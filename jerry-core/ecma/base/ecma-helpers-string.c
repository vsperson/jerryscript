/* Copyright 2014-2016 Samsung Electronics Co., Ltd.
 * Copyright 2016 University of Szeged.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** \addtogroup ecma ECMA
 * @{
 *
 * \addtogroup ecmahelpers Helpers for operations with ECMA data types
 * @{
 */

#include "ecma-alloc.h"
#include "ecma-gc.h"
#include "ecma-globals.h"
#include "ecma-helpers.h"
#include "ecma-lcache.h"
#include "jrt.h"
#include "jrt-libc-includes.h"
#include "lit-char-helpers.h"
#include "lit-literal.h"
#include "lit-magic-strings.h"
#include "lit-literal-storage.h"
#include "vm.h"

/**
 * Maximum length of strings' concatenation
 */
#define ECMA_STRING_MAX_CONCATENATION_LENGTH (CONFIG_ECMA_STRING_MAX_CONCATENATION_LENGTH)

/**
 * The length should be representable with int32_t.
 */
JERRY_STATIC_ASSERT ((int32_t) ECMA_STRING_MAX_CONCATENATION_LENGTH ==
                     ECMA_STRING_MAX_CONCATENATION_LENGTH,
                     ECMA_STRING_MAX_CONCATENATION_LENGTH_should_be_representable_with_int32_t);

/**
 * String header
 */
typedef struct
{
  uint16_t size; /* Size of string in bytes */
  uint16_t length; /* Number of characters in the string */
} ecma_string_heap_header_t;

static void
ecma_init_ecma_string_from_lit_cp (ecma_string_t *string_p,
                                   lit_cpointer_t lit_index);
static void
ecma_init_ecma_string_from_magic_string_id (ecma_string_t *string_p,
                                            lit_magic_string_id_t magic_string_id);

static void
ecma_init_ecma_string_from_magic_string_ex_id (ecma_string_t *string_p,
                                               lit_magic_string_ex_id_t magic_string_ex_id);

/**
 * Initialize ecma-string descriptor with string described by index in literal table
 */
static void
ecma_init_ecma_string_from_lit_cp (ecma_string_t *string_p, /**< descriptor to initialize */
                                   lit_cpointer_t lit_cp) /**< compressed pointer to literal */
{
  lit_literal_t lit = lit_cpointer_decompress (lit_cp);

  if (LIT_RECORD_IS_MAGIC_STR (lit))
  {
    ecma_init_ecma_string_from_magic_string_id (string_p,
                                                lit_magic_literal_get_magic_str_id (lit));

    return;
  }

  if (LIT_RECORD_IS_MAGIC_STR_EX (lit))
  {
    ecma_init_ecma_string_from_magic_string_ex_id (string_p,
                                                   lit_magic_literal_get_magic_str_ex_id (lit));
    return;
  }

  JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));

  string_p->refs = 1;
  string_p->container = ECMA_STRING_CONTAINER_LIT_TABLE;
  string_p->hash = lit_charset_literal_get_hash (lit);

  string_p->u.common_field = 0;
  string_p->u.lit_cp = lit_cp;
} /* ecma_init_ecma_string_from_lit_cp */

/**
 * Initialize ecma-string descriptor with specified magic string
 */
static void
ecma_init_ecma_string_from_magic_string_id (ecma_string_t *string_p, /**< descriptor to initialize */
                                            lit_magic_string_id_t magic_string_id) /**< identifier of
                                                                                         the magic string */
{
  string_p->refs = 1;
  string_p->container = ECMA_STRING_CONTAINER_MAGIC_STRING;
  string_p->hash = lit_utf8_string_calc_hash (lit_get_magic_string_utf8 (magic_string_id),
                                              lit_get_magic_string_size (magic_string_id));

  string_p->u.common_field = 0;
  string_p->u.magic_string_id = magic_string_id;
} /* ecma_init_ecma_string_from_magic_string_id */

/**
 * Initialize external ecma-string descriptor with specified magic string
 */
static void
ecma_init_ecma_string_from_magic_string_ex_id (ecma_string_t *string_p, /**< descriptor to initialize */
                                               lit_magic_string_ex_id_t magic_string_ex_id) /**< identifier of
                                                                                           the external magic string */
{
  string_p->refs = 1;
  string_p->container = ECMA_STRING_CONTAINER_MAGIC_STRING_EX;
  string_p->hash = lit_utf8_string_calc_hash (lit_get_magic_string_ex_utf8 (magic_string_ex_id),
                                              lit_get_magic_string_ex_size (magic_string_ex_id));

  string_p->u.common_field = 0;
  string_p->u.magic_string_ex_id = magic_string_ex_id;
} /* ecma_init_ecma_string_from_magic_string_ex_id */

/**
 * Allocate new ecma-string and fill it with characters from the utf8 string
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_utf8 (const lit_utf8_byte_t *string_p, /**< utf-8 string */
                                lit_utf8_size_t string_size) /**< string size */
{
  JERRY_ASSERT (string_p != NULL || string_size == 0);
  JERRY_ASSERT (lit_is_cesu8_string_valid (string_p, string_size));

  lit_magic_string_id_t magic_string_id;
  if (lit_is_utf8_string_magic (string_p, string_size, &magic_string_id))
  {
    return ecma_get_magic_string (magic_string_id);
  }

  lit_magic_string_ex_id_t magic_string_ex_id;
  if (lit_is_ex_utf8_string_magic (string_p, string_size, &magic_string_ex_id))
  {
    return ecma_get_magic_string_ex (magic_string_ex_id);
  }

  JERRY_ASSERT (string_size > 0);

  ecma_string_t *string_desc_p = ecma_alloc_string ();
  string_desc_p->refs = 1;
  string_desc_p->container = ECMA_STRING_CONTAINER_HEAP_CHUNKS;
  string_desc_p->hash = lit_utf8_string_calc_hash (string_p, string_size);

  string_desc_p->u.common_field = 0;

  const size_t data_size = string_size + sizeof (ecma_string_heap_header_t);
  ecma_string_heap_header_t *data_p = (ecma_string_heap_header_t *) mem_heap_alloc_block (data_size);
  data_p->size = (uint16_t) string_size;
  data_p->length = (uint16_t) lit_utf8_string_length (string_p, string_size);
  memcpy (data_p + 1, string_p, string_size);
  ECMA_SET_NON_NULL_POINTER (string_desc_p->u.collection_cp, data_p);

  return string_desc_p;
} /* ecma_new_ecma_string_from_utf8 */

/**
 * Allocate new ecma-string and fill it with cesu-8 character which represents specified code unit
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_code_unit (ecma_char_t code_unit) /**< code unit */
{
  lit_utf8_byte_t lit_utf8_bytes[LIT_UTF8_MAX_BYTES_IN_CODE_UNIT];
  lit_utf8_size_t bytes_size = lit_code_unit_to_utf8 (code_unit, lit_utf8_bytes);

  return ecma_new_ecma_string_from_utf8 (lit_utf8_bytes, bytes_size);
} /* ecma_new_ecma_string_from_code_unit */

/**
 * Allocate new ecma-string and fill it with ecma-number
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_uint32 (uint32_t uint32_number) /**< UInt32-represented ecma-number */
{
  ecma_string_t *string_desc_p = ecma_alloc_string ();
  string_desc_p->refs = 1;
  string_desc_p->container = ECMA_STRING_CONTAINER_UINT32_IN_DESC;

  lit_utf8_byte_t byte_buf[ECMA_MAX_CHARS_IN_STRINGIFIED_UINT32];
  ssize_t bytes_copied = ecma_uint32_to_utf8_string (uint32_number,
                                                     byte_buf,
                                                     ECMA_MAX_CHARS_IN_STRINGIFIED_UINT32);

  string_desc_p->hash = lit_utf8_string_calc_hash (byte_buf, (lit_utf8_size_t) bytes_copied);

  string_desc_p->u.common_field = 0;
  string_desc_p->u.uint32_number = uint32_number;

  return string_desc_p;
} /* ecma_new_ecma_string_from_uint32 */

/**
 * Allocate new ecma-string and fill it with ecma-number
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_number (ecma_number_t num) /**< ecma-number */
{
  uint32_t uint32_num = ecma_number_to_uint32 (num);
  if (num == ecma_uint32_to_number (uint32_num))
  {
    return ecma_new_ecma_string_from_uint32 (uint32_num);
  }

  lit_utf8_byte_t str_buf[ECMA_MAX_CHARS_IN_STRINGIFIED_NUMBER];
  lit_utf8_size_t str_size = ecma_number_to_utf8_string (num, str_buf, sizeof (str_buf));

  lit_magic_string_id_t magic_string_id;
  if (lit_is_utf8_string_magic (str_buf, str_size, &magic_string_id))
  {
    return ecma_get_magic_string (magic_string_id);
  }

  lit_magic_string_ex_id_t magic_string_ex_id;
  if (lit_is_ex_utf8_string_magic (str_buf, str_size, &magic_string_ex_id))
  {
    return ecma_get_magic_string_ex (magic_string_ex_id);
  }

  ecma_string_t *string_desc_p = ecma_alloc_string ();
  string_desc_p->refs = 1;
  string_desc_p->container = ECMA_STRING_CONTAINER_HEAP_NUMBER;
  string_desc_p->hash = lit_utf8_string_calc_hash (str_buf, str_size);

  string_desc_p->u.common_field = 0;
  ecma_number_t *num_p = ecma_alloc_number ();
  *num_p = num;
  ECMA_SET_POINTER (string_desc_p->u.number_cp, num_p);

  return string_desc_p;
} /* ecma_new_ecma_string_from_number */

/**
 * Allocate new ecma-string and fill it with reference to string literal
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_lit_cp (lit_cpointer_t lit_cp) /**< index in the literal table */
{
  ecma_string_t *string_desc_p = ecma_alloc_string ();

  ecma_init_ecma_string_from_lit_cp (string_desc_p, lit_cp);

  return string_desc_p;
} /* ecma_new_ecma_string_from_lit_cp */

/**
 * Allocate new ecma-string and fill it with reference to ECMA magic string
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_magic_string_id (lit_magic_string_id_t id) /**< identifier of magic string */
{
  JERRY_ASSERT (id < LIT_MAGIC_STRING__COUNT);

  ecma_string_t *string_desc_p = ecma_alloc_string ();
  ecma_init_ecma_string_from_magic_string_id (string_desc_p, id);

  return string_desc_p;
} /* ecma_new_ecma_string_from_magic_string_id */

/**
 * Allocate new ecma-string and fill it with reference to ECMA magic string
 *
 * @return pointer to ecma-string descriptor
 */
ecma_string_t *
ecma_new_ecma_string_from_magic_string_ex_id (lit_magic_string_ex_id_t id) /**< identifier of externl magic string */
{
  JERRY_ASSERT (id < lit_get_magic_string_ex_count ());

  ecma_string_t *string_desc_p = ecma_alloc_string ();
  ecma_init_ecma_string_from_magic_string_ex_id (string_desc_p, id);

  return string_desc_p;
} /* ecma_new_ecma_string_from_magic_string_ex_id */

/**
 * Concatenate ecma-strings
 *
 * @return concatenation of two ecma-strings
 */
ecma_string_t *
ecma_concat_ecma_strings (ecma_string_t *string1_p, /**< first ecma-string */
                          ecma_string_t *string2_p) /**< second ecma-string */
{
  JERRY_ASSERT (string1_p != NULL
                && string2_p != NULL);

  const lit_utf8_size_t str1_size = ecma_string_get_size (string1_p);
  const lit_utf8_size_t str2_size = ecma_string_get_size (string2_p);

  if (str1_size == 0)
  {
    return ecma_copy_or_ref_ecma_string (string2_p);
  }
  else if (str2_size == 0)
  {
    return ecma_copy_or_ref_ecma_string (string1_p);
  }

  const lit_utf8_size_t new_size = str1_size + str2_size;

  ecma_string_t *string_desc_p = ecma_alloc_string ();
  string_desc_p->refs = 1;
  string_desc_p->container = ECMA_STRING_CONTAINER_HEAP_CHUNKS;

  string_desc_p->u.common_field = 0;

  const size_t data_size = new_size + sizeof (ecma_string_heap_header_t);
  ecma_string_heap_header_t *data_p = (ecma_string_heap_header_t *) mem_heap_alloc_block (data_size);
  ssize_t bytes_copied1, bytes_copied2;

  bytes_copied1 = ecma_string_to_utf8_string (string1_p,
                                              (lit_utf8_byte_t *) (data_p + 1),
                                              (ssize_t) str1_size);
  JERRY_ASSERT (bytes_copied1 > 0);

  bytes_copied2 = ecma_string_to_utf8_string (string2_p,
                                              (lit_utf8_byte_t *) (data_p + 1) + str1_size,
                                              (ssize_t) str2_size);
  JERRY_ASSERT (bytes_copied2 > 0);

  data_p->size = (uint16_t) new_size;
  data_p->length = (uint16_t) (ecma_string_get_length (string1_p) + ecma_string_get_length (string2_p));
  string_desc_p->hash = lit_utf8_string_hash_combine (string1_p->hash,
                                                      (lit_utf8_byte_t *) (data_p + 1) + str1_size,
                                                      (lit_utf8_size_t) str2_size);

  ECMA_SET_NON_NULL_POINTER (string_desc_p->u.collection_cp, data_p);

  return string_desc_p;
} /* ecma_concat_ecma_strings */

/**
 * Copy ecma-string
 *
 * @return pointer to copy of ecma-string with reference counter set to 1
 */
static ecma_string_t *
ecma_copy_ecma_string (ecma_string_t *string_desc_p) /**< string descriptor */
{
  JERRY_ASSERT (string_desc_p != NULL);
  JERRY_ASSERT (string_desc_p->refs > 0);

  ecma_string_t *new_str_p;

  switch ((ecma_string_container_t) string_desc_p->container)
  {
    case ECMA_STRING_CONTAINER_LIT_TABLE:
    case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
    case ECMA_STRING_CONTAINER_MAGIC_STRING:
    case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
    {
      new_str_p = ecma_alloc_string ();

      *new_str_p = *string_desc_p;

      new_str_p->refs = 1;

      break;
    }

    case ECMA_STRING_CONTAINER_HEAP_NUMBER:
    {
      ecma_number_t *num_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t, string_desc_p->u.number_cp);

      new_str_p = ecma_new_ecma_string_from_number (*num_p);

      break;
    }

    case ECMA_STRING_CONTAINER_HEAP_CHUNKS:
    {
      new_str_p = ecma_alloc_string ();
      *new_str_p = *string_desc_p;

      const ecma_string_heap_header_t *data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                           string_desc_p->u.collection_cp);
      JERRY_ASSERT (data_p != NULL);
      const size_t data_size = data_p->size + sizeof (ecma_string_heap_header_t);
      ecma_string_heap_header_t *new_data_p = (ecma_string_heap_header_t *) mem_heap_alloc_block (data_size);
      memcpy (new_data_p, data_p, data_p->size + sizeof (ecma_string_heap_header_t));

      ECMA_SET_NON_NULL_POINTER (new_str_p->u.collection_cp, data_p);

      break;
    }

    default:
    {
      JERRY_UNREACHABLE ();
    }
  }

  JERRY_ASSERT (ecma_compare_ecma_strings (string_desc_p, new_str_p));

  return new_str_p;
} /* ecma_copy_ecma_string */

/**
 * Increase reference counter of ecma-string.
 *
 * @return pointer to same ecma-string descriptor with increased reference counter
 *         or the ecma-string's copy with reference counter set to 1
 */
ecma_string_t *
ecma_copy_or_ref_ecma_string (ecma_string_t *string_desc_p) /**< string descriptor */
{
  JERRY_ASSERT (string_desc_p != NULL);
  JERRY_ASSERT (string_desc_p->refs > 0);

  string_desc_p->refs++;

  if (unlikely (string_desc_p->refs == 0))
  {
    /* reference counter has overflowed */
    string_desc_p->refs--;

    uint32_t current_refs = string_desc_p->refs;

    /* First trying to free unreachable objects that maybe refer to the string */
    ecma_lcache_invalidate_all ();
    ecma_gc_run ();

    if (current_refs == string_desc_p->refs)
    {
      /* reference counter was not changed during GC, copying string */

      return ecma_copy_ecma_string (string_desc_p);
    }

    string_desc_p->refs++;

    JERRY_ASSERT (string_desc_p->refs != 0);
  }

  return string_desc_p;
} /* ecma_copy_or_ref_ecma_string */

/**
 * Decrease reference counter and deallocate ecma-string if
 * after that the counter the counter becomes zero.
 */
void
ecma_deref_ecma_string (ecma_string_t *string_p) /**< ecma-string */
{
  JERRY_ASSERT (string_p != NULL);
  JERRY_ASSERT (string_p->refs != 0);

  string_p->refs--;

  if (string_p->refs != 0)
  {
    return;
  }

  switch ((ecma_string_container_t) string_p->container)
  {
    case ECMA_STRING_CONTAINER_HEAP_CHUNKS:
    {
      ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                           string_p->u.collection_cp);

      mem_heap_free_block (data_p, data_p->size + sizeof (ecma_string_heap_header_t));

      break;
    }
    case ECMA_STRING_CONTAINER_HEAP_NUMBER:
    {
      ecma_number_t *num_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t,
                                                        string_p->u.number_cp);

      ecma_dealloc_number (num_p);

      break;
    }
    case ECMA_STRING_CONTAINER_LIT_TABLE:
    case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
    case ECMA_STRING_CONTAINER_MAGIC_STRING:
    case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
    {
      /* only the string descriptor itself should be freed */
    }
  }


  ecma_dealloc_string (string_p);
} /* ecma_deref_ecma_string */

/**
 * Assertion that specified ecma-string need not be freed
 */
void
ecma_check_that_ecma_string_need_not_be_freed (const ecma_string_t *string_p) /**< ecma-string descriptor */
{
#ifdef JERRY_NDEBUG
  (void) string_p;
#else /* JERRY_NDEBUG */

  /*
   * No reference counter increment or decrement
   * should be performed with ecma-string placed
   * on stack
   */
  JERRY_ASSERT (string_p->refs == 1);

  ecma_string_container_t container_type = (ecma_string_container_t) string_p->container;

  JERRY_ASSERT (container_type == ECMA_STRING_CONTAINER_LIT_TABLE ||
                container_type == ECMA_STRING_CONTAINER_MAGIC_STRING ||
                container_type == ECMA_STRING_CONTAINER_MAGIC_STRING_EX ||
                container_type == ECMA_STRING_CONTAINER_UINT32_IN_DESC);
#endif /* !JERRY_NDEBUG */
} /* ecma_check_that_ecma_string_need_not_be_freed */

/**
 * Convert ecma-string to number
 */
ecma_number_t
ecma_string_to_number (const ecma_string_t *str_p) /**< ecma-string */
{
  JERRY_ASSERT (str_p != NULL);

  switch ((ecma_string_container_t) str_p->container)
  {
    case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
    {
      uint32_t uint32_number = str_p->u.uint32_number;

      return ecma_uint32_to_number (uint32_number);
    }

    case ECMA_STRING_CONTAINER_HEAP_NUMBER:
    {
      ecma_number_t *num_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t,
                                                        str_p->u.number_cp);

      return *num_p;
    }

    case ECMA_STRING_CONTAINER_LIT_TABLE:
    case ECMA_STRING_CONTAINER_HEAP_CHUNKS:
    case ECMA_STRING_CONTAINER_MAGIC_STRING:
    case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
    {
      const lit_utf8_size_t string_size = ecma_string_get_size (str_p);

      if (string_size == 0)
      {
        return ECMA_NUMBER_ZERO;
      }

      ecma_number_t num;

      MEM_DEFINE_LOCAL_ARRAY (str_buffer_p, string_size, lit_utf8_byte_t);

      ssize_t bytes_copied = ecma_string_to_utf8_string (str_p,
                                                         str_buffer_p,
                                                         (ssize_t) string_size);
      JERRY_ASSERT (bytes_copied > 0);

      num = ecma_utf8_string_to_number (str_buffer_p, string_size);

      MEM_FINALIZE_LOCAL_ARRAY (str_buffer_p);

      return num;
    }
  }

  JERRY_UNREACHABLE ();
} /* ecma_string_to_number */

/**
 * Check if string is array index.
 *
 * @return true - if string is valid array index
 *         false - otherwise
 */
bool
ecma_string_get_array_index (const ecma_string_t *str_p, /**< ecma-string */
                             uint32_t *out_index_p) /**< [out] index */
{
  bool is_array_index = true;
  if (str_p->container == ECMA_STRING_CONTAINER_UINT32_IN_DESC)
  {
    *out_index_p = str_p->u.uint32_number;
  }
  else
  {
    ecma_number_t num = ecma_string_to_number (str_p);
    *out_index_p = ecma_number_to_uint32 (num);

    ecma_string_t *to_uint32_to_string_p = ecma_new_ecma_string_from_uint32 (*out_index_p);

    is_array_index = ecma_compare_ecma_strings (str_p,
                                                to_uint32_to_string_p);

    ecma_deref_ecma_string (to_uint32_to_string_p);
  }

  is_array_index = is_array_index && (*out_index_p != ECMA_MAX_VALUE_OF_VALID_ARRAY_INDEX);

  return is_array_index;
} /* ecma_string_get_array_index */

/**
 * Convert ecma-string's contents to a cesu-8 string and put it to the buffer.
 *
 * @return number of bytes, actually copied to the buffer - if string's content was copied successfully;
 *         otherwise (in case size of buffer is insufficient) - negative number, which is calculated
 *         as negation of buffer size, that is required to hold the string's content.
 */
ssize_t __attr_return_value_should_be_checked___
ecma_string_to_utf8_string (const ecma_string_t *string_desc_p, /**< ecma-string descriptor */
                            lit_utf8_byte_t *buffer_p, /**< destination buffer pointer
                                                        * (can be NULL if buffer_size == 0) */
                            ssize_t buffer_size) /**< size of buffer */
{
  JERRY_ASSERT (string_desc_p != NULL);
  JERRY_ASSERT (string_desc_p->refs > 0);
  JERRY_ASSERT (buffer_p != NULL || buffer_size == 0);
  JERRY_ASSERT (buffer_size >= 0);

  ssize_t required_buffer_size = (ssize_t) ecma_string_get_size (string_desc_p);

  if (required_buffer_size > buffer_size
      || buffer_size == 0)
  {
    return -required_buffer_size;
  }

  switch ((ecma_string_container_t) string_desc_p->container)
  {
    case ECMA_STRING_CONTAINER_HEAP_CHUNKS:
    {
      const ecma_string_heap_header_t *data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                           string_desc_p->u.collection_cp);

      memcpy (buffer_p, data_p + 1, (size_t) data_p->size);
      break;
    }
    case ECMA_STRING_CONTAINER_LIT_TABLE:
    {
      lit_literal_t lit = lit_get_literal_by_cp (string_desc_p->u.lit_cp);
      JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));
      lit_literal_to_utf8_string (lit, buffer_p, (size_t) required_buffer_size);
      break;
    }
    case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
    {
      uint32_t uint32_number = string_desc_p->u.uint32_number;
      ssize_t bytes_copied = ecma_uint32_to_utf8_string (uint32_number, buffer_p, required_buffer_size);

      JERRY_ASSERT (bytes_copied == required_buffer_size);

      break;
    }
    case ECMA_STRING_CONTAINER_HEAP_NUMBER:
    {
      ecma_number_t *num_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t,
                                                        string_desc_p->u.number_cp);

      lit_utf8_size_t size = ecma_number_to_utf8_string (*num_p, buffer_p, buffer_size);

      JERRY_ASSERT (required_buffer_size == (ssize_t) size);

      break;
    }
    case ECMA_STRING_CONTAINER_MAGIC_STRING:
    {
      const lit_magic_string_id_t id = string_desc_p->u.magic_string_id;
      const lit_utf8_size_t bytes_to_copy = lit_get_magic_string_size (id);

      memcpy (buffer_p, lit_get_magic_string_utf8 (id), bytes_to_copy);

      JERRY_ASSERT (required_buffer_size == (ssize_t) bytes_to_copy);

      break;
    }
    case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
    {
      const lit_magic_string_ex_id_t id = string_desc_p->u.magic_string_ex_id;
      const size_t bytes_to_copy = lit_get_magic_string_ex_size (id);

      memcpy (buffer_p, lit_get_magic_string_ex_utf8 (id), bytes_to_copy);

      JERRY_ASSERT (required_buffer_size == (ssize_t) bytes_to_copy);

      break;
    }
  }

  return required_buffer_size;
} /* ecma_string_to_utf8_string */

/**
 * Long path part of ecma-string to ecma-string comparison routine
 *
 * See also:
 *          ecma_compare_ecma_strings
 *
 * @return true - if strings are equal;
 *         false - otherwise.
 */
static bool __attr_noinline___
ecma_compare_ecma_strings_longpath (const ecma_string_t *string1_p, /* ecma-string */
                                    const ecma_string_t *string2_p) /* ecma-string */
{
  if (string1_p->container == string2_p->container)
  {
    switch ((ecma_string_container_t) string1_p->container)
    {
      case ECMA_STRING_CONTAINER_LIT_TABLE:
      {
        JERRY_ASSERT (string1_p->u.lit_cp != string2_p->u.lit_cp);
        return false;
      }
      case ECMA_STRING_CONTAINER_MAGIC_STRING:
      {
        JERRY_ASSERT (string1_p->u.magic_string_id != string2_p->u.magic_string_id);
        return false;
      }
      case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
      {
        JERRY_ASSERT (string1_p->u.magic_string_ex_id != string2_p->u.magic_string_ex_id);
        return false;
      }
      case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
      {
        JERRY_ASSERT (string1_p->u.uint32_number != string2_p->u.uint32_number);
        return false;
      }
      default:
      {
        JERRY_ASSERT (string1_p->container == ECMA_STRING_CONTAINER_HEAP_NUMBER
                      || string1_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS);
        break;
      }
    }
  }

  const lit_utf8_size_t string1_size = ecma_string_get_size (string1_p);
  const lit_utf8_size_t string2_size = ecma_string_get_size (string2_p);

  if (string1_size != string2_size)
  {
    return false;
  }

  const lit_utf8_size_t strings_size = string1_size;

  if (strings_size == 0)
  {
    return true;
  }

  if (string1_p->container == string2_p->container)
  {
    switch ((ecma_string_container_t) string1_p->container)
    {
      case ECMA_STRING_CONTAINER_HEAP_NUMBER:
      {
        ecma_number_t *num1_p, *num2_p;
        num1_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t, string1_p->u.number_cp);
        num2_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t, string2_p->u.number_cp);

        if (ecma_number_is_nan (*num1_p)
            && ecma_number_is_nan (*num2_p))
        {
          return true;
        }

        return (*num1_p == *num2_p);
      }
      case ECMA_STRING_CONTAINER_HEAP_CHUNKS:
      {
        const ecma_string_heap_header_t *data1_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                              string1_p->u.collection_cp);
        const ecma_string_heap_header_t *data2_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                              string2_p->u.collection_cp);

        if (data1_p->length != data2_p->length)
        {
          return false;
        }

        return !strncmp ((char *) (data1_p + 1), (char *) (data2_p + 1), strings_size);
      }
      default:
      {
        JERRY_UNREACHABLE ();
        break;
      }
    }
  }

  lit_utf8_byte_t *utf8_string1_p, *utf8_string2_p;
  bool is_utf8_string1_on_heap = false;
  bool is_utf8_string2_on_heap = false;

  if (string1_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS)
  {
    const ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                               string1_p->u.collection_cp);

    utf8_string1_p = (lit_utf8_byte_t *) (data_p + 1);
  }
  else if (string1_p->container == ECMA_STRING_CONTAINER_LIT_TABLE)
  {
    const lit_literal_t lit = lit_get_literal_by_cp (string1_p->u.lit_cp);
    JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));

    utf8_string1_p = (lit_utf8_byte_t *) lit_charset_literal_get_charset (lit);
  }
  else
  {
    utf8_string1_p = (lit_utf8_byte_t *) mem_heap_alloc_block ((size_t) strings_size);

    ssize_t bytes_copied = ecma_string_to_utf8_string (string1_p, utf8_string1_p, (ssize_t) strings_size);
    JERRY_ASSERT (bytes_copied > 0);

    is_utf8_string1_on_heap = true;
  }

  if (string2_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS)
  {
    const ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                               string2_p->u.collection_cp);

    utf8_string2_p = (lit_utf8_byte_t *) (data_p + 1);
  }
  else if (string2_p->container == ECMA_STRING_CONTAINER_LIT_TABLE)
  {
    const lit_literal_t lit = lit_get_literal_by_cp (string2_p->u.lit_cp);
    JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));

    utf8_string2_p = (lit_utf8_byte_t *) lit_charset_literal_get_charset (lit);
  }
  else
  {
    utf8_string2_p = (lit_utf8_byte_t *) mem_heap_alloc_block ((size_t) strings_size);

    ssize_t bytes_copied = ecma_string_to_utf8_string (string2_p, utf8_string2_p, (ssize_t) strings_size);
    JERRY_ASSERT (bytes_copied > 0);

    is_utf8_string2_on_heap = true;
  }
  const bool is_equal = !strncmp ((char *) utf8_string1_p, (char *) utf8_string2_p, (size_t) strings_size);

  if (is_utf8_string1_on_heap)
  {
    mem_heap_free_block ((void *) utf8_string1_p, (size_t) strings_size);
  }

  if (is_utf8_string2_on_heap)
  {
    mem_heap_free_block ((void *) utf8_string2_p, (size_t) strings_size);
  }

  return is_equal;
} /* ecma_compare_ecma_strings_longpath */

/**
 * Compare ecma-string to ecma-string if they're hashes are equal
 *
 * @return true - if strings are equal;
 *         false - may be.
 */
bool
ecma_compare_ecma_strings_equal_hashes (const ecma_string_t *string1_p, /* ecma-string */
                                        const ecma_string_t *string2_p) /* ecma-string */
{
  JERRY_ASSERT (string1_p->hash == string2_p->hash);

  if (string1_p->container == string2_p->container
      && string1_p->u.common_field == string2_p->u.common_field)
  {
    return true;
  }
  else
  {
    return false;
  }
} /* ecma_compare_ecma_strings_equal_hashes */

/**
 * Compare ecma-string to ecma-string
 *
 * @return true - if strings are equal;
 *         false - otherwise.
 */
bool
ecma_compare_ecma_strings (const ecma_string_t *string1_p, /* ecma-string */
                           const ecma_string_t *string2_p) /* ecma-string */
{
  JERRY_ASSERT (string1_p != NULL && string2_p != NULL);

  const bool is_equal_hashes = (string1_p->hash == string2_p->hash);

  if (!is_equal_hashes)
  {
    return false;
  }
  const bool is_equal_containers = (string1_p->container == string2_p->container);
  const bool is_equal_fields = (string1_p->u.common_field == string2_p->u.common_field);

  if (is_equal_containers && is_equal_fields)
  {
    return true;
  }
  else
  {
    return ecma_compare_ecma_strings_longpath (string1_p, string2_p);
  }
} /* ecma_compare_ecma_strings */

/**
 * Relational compare of ecma-strings.
 *
 * First string is less than second string if:
 *  - strings are not equal;
 *  - first string is prefix of second or is lexicographically less than second.
 *
 * @return true - if first string is less than second string,
 *         false - otherwise.
 */
bool
ecma_compare_ecma_strings_relational (const ecma_string_t *string1_p, /**< ecma-string */
                                      const ecma_string_t *string2_p) /**< ecma-string */
{
  if (ecma_compare_ecma_strings (string1_p,
                                 string2_p))
  {
    return false;
  }

  const lit_utf8_byte_t *utf8_string1_p, *utf8_string2_p;
  bool is_utf8_string1_on_heap = false, is_utf8_string2_on_heap = false;
  lit_utf8_byte_t utf8_string1_buffer[ECMA_MAX_CHARS_IN_STRINGIFIED_NUMBER];
  lit_utf8_size_t utf8_string1_size;
  lit_utf8_byte_t utf8_string2_buffer[ECMA_MAX_CHARS_IN_STRINGIFIED_NUMBER];
  lit_utf8_size_t utf8_string2_size;

  if (string1_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS)
  {
    const ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                         string1_p->u.collection_cp);

    utf8_string1_p = (lit_utf8_byte_t *) (data_p + 1);
    utf8_string1_size = (lit_utf8_size_t) data_p->size;
  }
  else if (string1_p->container == ECMA_STRING_CONTAINER_LIT_TABLE)
  {
    const lit_literal_t lit = lit_get_literal_by_cp (string1_p->u.lit_cp);
    JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));

    utf8_string1_p = (lit_utf8_byte_t *) lit_charset_literal_get_charset (lit);
    utf8_string1_size = (lit_utf8_size_t) lit_charset_literal_get_size (lit);
  }
  else
  {
    const ssize_t req_size = ecma_string_to_utf8_string (string1_p, utf8_string1_buffer, sizeof (utf8_string1_buffer));

    if (req_size < 0)
    {
      lit_utf8_byte_t *heap_buffer_p = (lit_utf8_byte_t *) mem_heap_alloc_block ((size_t) -req_size);

      ssize_t bytes_copied = ecma_string_to_utf8_string (string1_p, heap_buffer_p, -req_size);
      utf8_string1_size = (lit_utf8_size_t) bytes_copied;

      JERRY_ASSERT (bytes_copied > 0);

      utf8_string1_p = heap_buffer_p;
      is_utf8_string1_on_heap = true;
    }
    else
    {
      utf8_string1_p = utf8_string1_buffer;
      utf8_string1_size = (lit_utf8_size_t) req_size;
    }
  }

  if (string2_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS)
  {
    const ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                         string2_p->u.collection_cp);

    utf8_string2_p = (lit_utf8_byte_t *) (data_p + 1);
    utf8_string2_size = (lit_utf8_size_t) data_p->size;
  }
  else if (string2_p->container == ECMA_STRING_CONTAINER_LIT_TABLE)
  {
    const lit_literal_t lit = lit_get_literal_by_cp (string2_p->u.lit_cp);
    JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));

    utf8_string2_p = (lit_utf8_byte_t *) lit_charset_literal_get_charset (lit);
    utf8_string2_size = (lit_utf8_size_t) lit_charset_literal_get_size (lit);
  }
  else
  {
    const ssize_t req_size = ecma_string_to_utf8_string (string2_p, utf8_string2_buffer, sizeof (utf8_string2_buffer));
    if (req_size < 0)
    {
      lit_utf8_byte_t *heap_buffer_p = (lit_utf8_byte_t *) mem_heap_alloc_block ((size_t) -req_size);

      ssize_t bytes_copied = ecma_string_to_utf8_string (string2_p, heap_buffer_p, -req_size);
      utf8_string2_size = (lit_utf8_size_t) bytes_copied;

      JERRY_ASSERT (bytes_copied > 0);

      utf8_string2_p = heap_buffer_p;
      is_utf8_string2_on_heap = true;
    }
    else
    {
      utf8_string2_p = utf8_string2_buffer;
      utf8_string2_size = (lit_utf8_size_t) req_size;
    }
  }

  bool is_first_less_than_second = lit_compare_utf8_strings_relational (utf8_string1_p,
                                                                        utf8_string1_size,
                                                                        utf8_string2_p,
                                                                        utf8_string2_size);

  if (is_utf8_string1_on_heap)
  {
    mem_heap_free_block ((void *) utf8_string1_p, (size_t) utf8_string1_size);
  }

  if (is_utf8_string2_on_heap)
  {
    mem_heap_free_block ((void *) utf8_string2_p, (size_t) utf8_string2_size);
  }

  return is_first_less_than_second;
} /* ecma_compare_ecma_strings_relational */

/**
 * Lengths for numeric string values
 */
static const uint32_t nums_with_ascending_length[] =
{
  1u,
  10u,
  100u,
  1000u,
  10000u,
  100000u,
  1000000u,
  10000000u,
  100000000u,
  1000000000u
};

/**
 * Maximum length of numeric strings
 */
static const uint32_t max_uint32_len = (uint32_t) (sizeof (nums_with_ascending_length) / sizeof (uint32_t));

/**
 * Get size of the number stored locally in the string's descriptor
 *
 * Note: the represented number size and length are equal
 *
 * @return size in bytes
 */
static ecma_length_t __attr_always_inline___
ecma_string_get_number_in_desc_size (const uint32_t uint32_number) /**< number in the string-descriptor */
{
  ecma_length_t size = 1;

  while (size < max_uint32_len && uint32_number >= nums_with_ascending_length[size])
  {
    size++;
  }
  return size;
} /* ecma_string_get_number_in_desc_size */

/**
 * Get size of container heap number of ecma-string
 *
 * Note: the number size and length are equal
 *
 * @return number of bytes in the buffer
 */
static lit_utf8_size_t __attr_always_inline___
ecma_string_get_heap_number_size (mem_cpointer_t number_cp) /**< Compressed pointer to an ecma_number_t */
{
  const ecma_number_t *num_p = ECMA_GET_NON_NULL_POINTER (ecma_number_t, number_cp);
  lit_utf8_byte_t buffer[ECMA_MAX_CHARS_IN_STRINGIFIED_NUMBER];

  return ecma_number_to_utf8_string (*num_p, buffer, sizeof (buffer));
} /* ecma_string_get_heap_number_size */

/**
 * Get length of ecma-string
 *
 * @return number of characters in the string
 */
ecma_length_t
ecma_string_get_length (const ecma_string_t *string_p) /**< ecma-string */
{
  switch ((ecma_string_container_t) string_p->container)
  {
    case ECMA_STRING_CONTAINER_LIT_TABLE:
    {
      lit_literal_t lit = lit_get_literal_by_cp (string_p->u.lit_cp);
      JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));
      return lit_charset_literal_get_length (lit);
    }
    case ECMA_STRING_CONTAINER_MAGIC_STRING:
    {
      TODO ("Cache magic string lengths")
      return lit_utf8_string_length (lit_get_magic_string_utf8 (string_p->u.magic_string_id),
                                     lit_get_magic_string_size (string_p->u.magic_string_id));
    }
    case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
    {
      TODO ("Cache magic string lengths")
      return lit_utf8_string_length (lit_get_magic_string_ex_utf8 (string_p->u.magic_string_ex_id),
                                     lit_get_magic_string_ex_size (string_p->u.magic_string_ex_id));
    }
    case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
    {
      return ecma_string_get_number_in_desc_size (string_p->u.uint32_number);
    }
    case ECMA_STRING_CONTAINER_HEAP_NUMBER:
    {
      return (ecma_length_t) ecma_string_get_heap_number_size (string_p->u.number_cp);
    }
    default:
    {
      JERRY_ASSERT ((ecma_string_container_t) string_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS);
      const ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                                 string_p->u.collection_cp);

      return (ecma_length_t) data_p->length;
    }
  }
} /* ecma_string_get_length */

/**
 * Get size of ecma-string
 *
 * @return number of bytes in the buffer needed to represent the string
 */
lit_utf8_size_t
ecma_string_get_size (const ecma_string_t *string_p) /**< ecma-string */
{
  switch ((ecma_string_container_t) string_p->container)
  {
    case ECMA_STRING_CONTAINER_LIT_TABLE:
    {
      lit_literal_t lit = lit_get_literal_by_cp (string_p->u.lit_cp);
      JERRY_ASSERT (LIT_RECORD_IS_CHARSET (lit));

      return lit_charset_literal_get_size (lit);
    }
    case ECMA_STRING_CONTAINER_MAGIC_STRING:
    {
      return lit_get_magic_string_size (string_p->u.magic_string_id);
    }
    case ECMA_STRING_CONTAINER_MAGIC_STRING_EX:
    {
      return lit_get_magic_string_ex_size (string_p->u.magic_string_ex_id);
    }
    case ECMA_STRING_CONTAINER_UINT32_IN_DESC:
    {
      return (lit_utf8_size_t) ecma_string_get_number_in_desc_size (string_p->u.uint32_number);
    }
    case ECMA_STRING_CONTAINER_HEAP_NUMBER:
    {
      return ecma_string_get_heap_number_size (string_p->u.number_cp);
    }
    default:
    {
      JERRY_ASSERT ((ecma_string_container_t) string_p->container == ECMA_STRING_CONTAINER_HEAP_CHUNKS);
      const ecma_string_heap_header_t *const data_p = ECMA_GET_NON_NULL_POINTER (ecma_string_heap_header_t,
                                                                                 string_p->u.collection_cp);

      return (lit_utf8_size_t) data_p->size;
    }
  }
} /* ecma_string_get_size */

/**
 * Get character from specified position in the ecma-string.
 *
 * @return character value
 */
ecma_char_t
ecma_string_get_char_at_pos (const ecma_string_t *string_p, /**< ecma-string */
                             ecma_length_t index) /**< index of character */
{
  ecma_length_t string_length = ecma_string_get_length (string_p);
  JERRY_ASSERT (index < string_length);

  lit_utf8_size_t buffer_size = ecma_string_get_size (string_p);

  ecma_char_t ch;

  MEM_DEFINE_LOCAL_ARRAY (utf8_str_p, buffer_size, lit_utf8_byte_t);

  ssize_t sz = ecma_string_to_utf8_string (string_p, utf8_str_p, (ssize_t) buffer_size);
  JERRY_ASSERT (sz > 0);

  ch = lit_utf8_string_code_unit_at (utf8_str_p, buffer_size, index);

  MEM_FINALIZE_LOCAL_ARRAY (utf8_str_p);

  return ch;
} /* ecma_string_get_char_at_pos */

/**
 * Get byte from specified position in the ecma-string.
 *
 * @return byte value
 */
lit_utf8_byte_t
ecma_string_get_byte_at_pos (const ecma_string_t *string_p, /**< ecma-string */
                             lit_utf8_size_t index) /**< byte index */
{
  lit_utf8_size_t buffer_size = ecma_string_get_size (string_p);
  JERRY_ASSERT (index < (lit_utf8_size_t) buffer_size);

  lit_utf8_byte_t byte;

  MEM_DEFINE_LOCAL_ARRAY (utf8_str_p, buffer_size, lit_utf8_byte_t);

  ssize_t sz = ecma_string_to_utf8_string (string_p, utf8_str_p, (ssize_t) buffer_size);
  JERRY_ASSERT (sz > 0);

  byte = utf8_str_p[index];

  MEM_FINALIZE_LOCAL_ARRAY (utf8_str_p);

  return byte;
} /* ecma_string_get_byte_at_pos */

/**
 * Get specified magic string
 *
 * @return ecma-string containing specified magic string
 */
ecma_string_t *
ecma_get_magic_string (lit_magic_string_id_t id) /**< magic string id */
{
  return ecma_new_ecma_string_from_magic_string_id (id);
} /* ecma_get_magic_string */

/**
 * Get specified external magic string
 *
 * @return ecma-string containing specified external magic string
 */
ecma_string_t *
ecma_get_magic_string_ex (lit_magic_string_ex_id_t id) /**< external magic string id */
{
  return ecma_new_ecma_string_from_magic_string_ex_id (id);
} /* ecma_get_magic_string_ex */

/**
 * Long path part of ecma_is_string_magic
 *
 * Converts passed ecma-string to zt-string and
 * checks if it is equal to one of magic string
 *
 * @return true - if magic string equal to passed string was found,
 *         false - otherwise.
 */
static bool
ecma_is_string_magic_longpath (const ecma_string_t *string_p, /**< ecma-string */
                               lit_magic_string_id_t *out_id_p) /**< [out] magic string's id */
{
  lit_utf8_byte_t utf8_string_buffer[LIT_MAGIC_STRING_LENGTH_LIMIT];

  ssize_t copied = ecma_string_to_utf8_string (string_p, utf8_string_buffer, (ssize_t) sizeof (utf8_string_buffer));
  JERRY_ASSERT (copied > 0);

  return lit_is_utf8_string_magic (utf8_string_buffer, (lit_utf8_size_t) copied, out_id_p);
} /* ecma_is_string_magic_longpath */

/**
 * Long path part of ecma_is_ex_string_magic
 *
 * Converts passed ecma-string to zt-string and
 * checks if it is equal to one of magic string
 *
 * @return true - if magic string equal to passed string was found,
 *         false - otherwise.
 */
static bool
ecma_is_ex_string_magic_longpath (const ecma_string_t *string_p, /**< ecma-string */
                                  lit_magic_string_ex_id_t *out_id_p) /**< [out] external magic string's id */
{
  lit_utf8_byte_t utf8_string_buffer[LIT_MAGIC_STRING_LENGTH_LIMIT];

  ssize_t copied = ecma_string_to_utf8_string (string_p, utf8_string_buffer, (ssize_t) sizeof (utf8_string_buffer));
  JERRY_ASSERT (copied > 0);

  return lit_is_ex_utf8_string_magic (utf8_string_buffer, (lit_utf8_size_t) copied, out_id_p);
} /* ecma_is_ex_string_magic_longpath */

/**
 * Check if passed string equals to one of magic strings
 * and if equal magic string was found, return it's id in 'out_id_p' argument.
 *
 * @return true - if magic string equal to passed string was found,
 *         false - otherwise.
 */
bool
ecma_is_string_magic (const ecma_string_t *string_p, /**< ecma-string */
                      lit_magic_string_id_t *out_id_p) /**< [out] magic string's id */
{
  if (string_p->container == ECMA_STRING_CONTAINER_MAGIC_STRING)
  {
    JERRY_ASSERT (string_p->u.magic_string_id < LIT_MAGIC_STRING__COUNT);

    *out_id_p = (lit_magic_string_id_t) string_p->u.magic_string_id;

    return true;
  }
  else
  {
    /*
     * Any ecma-string constructor except ecma_concat_ecma_strings
     * should return ecma-string with ECMA_STRING_CONTAINER_MAGIC_STRING
     * container type if new ecma-string's content is equal to one of magic strings.
     */
    JERRY_ASSERT (ecma_string_get_length (string_p) > LIT_MAGIC_STRING_LENGTH_LIMIT
                  || !ecma_is_string_magic_longpath (string_p, out_id_p));

    return false;
  }
} /* ecma_is_string_magic */

/**
 * Check if passed string equals to one of external magic strings
 * and if equal external magic string was found, return it's id in 'out_id_p' argument.
 *
 * @return true - if external magic string equal to passed string was found,
 *         false - otherwise.
 */
bool
ecma_is_ex_string_magic (const ecma_string_t *string_p, /**< ecma-string */
                         lit_magic_string_ex_id_t *out_id_p) /**< [out] external magic string's id */
{
  if (string_p->container == ECMA_STRING_CONTAINER_MAGIC_STRING_EX)
  {
    JERRY_ASSERT (string_p->u.magic_string_ex_id < lit_get_magic_string_ex_count ());

    *out_id_p = (lit_magic_string_ex_id_t) string_p->u.magic_string_ex_id;

    return true;
  }
  else
  {
    /*
     * Any ecma-string constructor except ecma_concat_ecma_strings
     * should return ecma-string with ECMA_STRING_CONTAINER_MAGIC_STRING_EX
     * container type if new ecma-string's content is equal to one of external magic strings.
     */
    JERRY_ASSERT (ecma_string_get_length (string_p) > LIT_MAGIC_STRING_LENGTH_LIMIT
                  || !ecma_is_ex_string_magic_longpath (string_p, out_id_p));

    return false;
  }
} /* ecma_is_ex_string_magic */

/**
 * Try to calculate hash of the ecma-string
 *
 * @return calculated hash
 */
lit_string_hash_t
ecma_string_hash (const ecma_string_t *string_p) /**< ecma-string to calculate hash for */

{
  return (string_p->hash);
} /* ecma_string_hash */

/**
 * Create a substring from an ecma string
 *
 * @return a newly consturcted ecma string with its value initialized to a copy of a substring of the first argument
 */
ecma_string_t *
ecma_string_substr (const ecma_string_t *string_p, /**< pointer to an ecma string */
                    ecma_length_t start_pos, /**< start position, should be less or equal than string length */
                    ecma_length_t end_pos) /**< end position, should be less or equal than string length */
{
#ifndef JERRY_NDEBUG
  const ecma_length_t string_length = ecma_string_get_length (string_p);
  JERRY_ASSERT (start_pos <= string_length);
  JERRY_ASSERT (end_pos <= string_length);
#endif

  if (start_pos < end_pos)
  {
    /**
     * I. Dump original string to plain buffer
     */
    ecma_string_t *ecma_string_p;

    lit_utf8_size_t buffer_size = ecma_string_get_size (string_p);
    MEM_DEFINE_LOCAL_ARRAY (utf8_str_p, buffer_size, lit_utf8_byte_t);

    ssize_t sz = ecma_string_to_utf8_string (string_p, utf8_str_p, (ssize_t) buffer_size);
    JERRY_ASSERT (sz >= 0);

    /**
     * II. Extract substring
     */
    lit_utf8_byte_t *start_p = utf8_str_p;
    end_pos -= start_pos;

    while (start_pos--)
    {
      start_p += lit_get_unicode_char_size_by_utf8_first_byte (*start_p);
    }

    lit_utf8_byte_t *end_p = start_p;
    while (end_pos--)
    {
      end_p += lit_get_unicode_char_size_by_utf8_first_byte (*end_p);
    }

    ecma_string_p = ecma_new_ecma_string_from_utf8 (start_p, (lit_utf8_size_t) (end_p - start_p));

    MEM_FINALIZE_LOCAL_ARRAY (utf8_str_p);

    return ecma_string_p;
  }
  else
  {
    return ecma_new_ecma_string_from_utf8 (NULL, 0);
  }

  JERRY_UNREACHABLE ();
} /* ecma_string_substr */

/**
 * Trim leading and trailing whitespace characters from string.
 *
 * @return trimmed ecma string
 */
ecma_string_t *
ecma_string_trim (const ecma_string_t *string_p) /**< pointer to an ecma string */
{
  ecma_string_t *ret_string_p;

  lit_utf8_size_t buffer_size = ecma_string_get_size (string_p);

  if (buffer_size > 0)
  {
    MEM_DEFINE_LOCAL_ARRAY (utf8_str_p, buffer_size, lit_utf8_byte_t);

    ssize_t sz = ecma_string_to_utf8_string (string_p, utf8_str_p, (ssize_t) buffer_size);
    JERRY_ASSERT (sz >= 0);

    ecma_char_t ch;
    lit_utf8_size_t read_size;
    lit_utf8_byte_t *nonws_start_p = utf8_str_p + buffer_size;
    lit_utf8_byte_t *current_p = utf8_str_p;

    /* Trim front. */
    while (current_p < nonws_start_p)
    {
      read_size = lit_read_code_unit_from_utf8 (current_p, &ch);

      if (!lit_char_is_white_space (ch)
          && !lit_char_is_line_terminator (ch))
      {
        nonws_start_p = current_p;
        break;
      }

      current_p += read_size;
    }

    current_p = utf8_str_p + buffer_size;

    /* Trim back. */
    while (current_p > utf8_str_p)
    {
      read_size = lit_read_prev_code_unit_from_utf8 (current_p, &ch);

      if (!lit_char_is_white_space (ch)
          && !lit_char_is_line_terminator (ch))
      {
        break;
      }

      current_p -= read_size;
    }

    /* Construct new string. */
    if (current_p > nonws_start_p)
    {
      ret_string_p = ecma_new_ecma_string_from_utf8 (nonws_start_p,
                                                     (lit_utf8_size_t) (current_p - nonws_start_p));
    }
    else
    {
      ret_string_p = ecma_get_magic_string (LIT_MAGIC_STRING__EMPTY);
    }

    MEM_FINALIZE_LOCAL_ARRAY (utf8_str_p);
  }
  else
  {
    ret_string_p = ecma_get_magic_string (LIT_MAGIC_STRING__EMPTY);
  }

  return ret_string_p;

} /* ecma_string_trim */

/**
 * @}
 * @}
 */
