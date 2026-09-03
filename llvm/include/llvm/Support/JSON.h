//===--- JSON.h - JSON values, parsing and serialization -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
///
/// \file
/// This file supports working with JSON data.
///
/// It comprises:
///
/// - classes which hold dynamically-typed parsed JSON structures
///   These are value types that can be composed, inspected, and modified.
///   See json::Value, and the related types json::Object and json::Array.
///
/// - functions to parse JSON text into Values, and to serialize Values to text.
///   See parse(), operator<<, and format_provider.
///
/// - a convention and helpers for mapping between json::Value and user-defined
///   types. See fromJSON(), ObjectMapper, and the class comment on Value.
///
/// - an output API json::OStream which can emit JSON without materializing
///   all structures as json::Value.
///
/// Typically, JSON data would be read from an external source, parsed into
/// a Value, and then converted into some native data structure before doing
/// real work on it. (And vice versa when writing).
///
/// Other serialization mechanisms you may consider:
///
/// - YAML is also text-based, and more human-readable than JSON. It's a more
///   complex format and data model, and YAML parsers aren't ubiquitous.
///   YAMLParser.h is a streaming parser suitable for parsing large documents
///   (including JSON, as YAML is a superset). It can be awkward to use
///   directly. YAML I/O (YAMLTraits.h) provides data mapping that is more
///   declarative than the toJSON/fromJSON conventions here.
///
/// - LLVM bitstream is a space- and CPU- efficient binary format. Typically it
///   encodes LLVM IR ("bitcode"), but it can be a container for other data.
///   Low-level reader/writer libraries are in Bitstream/Bitstream*.h
///
//===---------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_JSON_H
#define LLVM_SUPPORT_JSON_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>
#include <map>

namespace llvm {
namespace json {

// === String encodings ===
//
// JSON strings are character sequences (not byte sequences like std::string).
// We need to know the encoding, and for simplicity only support UTF-8.
//
//   - When parsing, invalid UTF-8 is a syntax error like any other
//
//   - When creating Values from strings, callers must ensure they are UTF-8.
//        with asserts on, invalid UTF-8 will crash the program
//        with asserts off, we'll substitute the replacement character (U+FFFD)
//     Callers can use json::isUTF8() and json::fixUTF8() for validation.
//
//   - When retrieving strings from Values (e.g. asString()), the result will
//     always be valid UTF-8.

/// True when \c T is an unsigned integer type that is exactly 64 bits wide.
template <typename T>
constexpr bool is_uint_64_bit_v =
    std::is_integral_v<T> && std::is_unsigned_v<T> &&
    sizeof(T) == sizeof(uint64_t);

/// Returns true if \p S is valid UTF-8, which is required for use as JSON.
///
/// If it returns false, \p Offset is set to a byte offset near the first error.
///
/// \param S String to validate.
/// \param ErrOffset Optional output for a byte offset near the first error.
/// \return True if \p S is valid UTF-8.
LLVM_ABI bool isUTF8(llvm::StringRef S, size_t *ErrOffset = nullptr);
/// Replace invalid UTF-8 sequences in \p S with U+FFFD.
///
/// The returned string is valid UTF-8.
/// This is much slower than isUTF8, so test that first.
///
/// \param S String that may contain invalid UTF-8.
/// \return A valid UTF-8 string with invalid sequences replaced.
LLVM_ABI std::string fixUTF8(llvm::StringRef S);

class Array;
class ObjectKey;
class Value;
template <typename T> Value toJSON(const std::optional<T> &Opt);

/// An Object is a JSON object, which maps strings to heterogenous JSON values.
/// It simulates DenseMap<ObjectKey, Value>. ObjectKey is a maybe-owned string.
class Object {
  using Storage = DenseMap<ObjectKey, Value, llvm::DenseMapInfo<StringRef>>;
  Storage M;

public:
  /// Type of object keys.
  using key_type = ObjectKey;
  /// Type of mapped JSON values.
  using mapped_type = Value;
  /// Key/value pair type stored in the object.
  using value_type = Storage::value_type;
  /// Mutable iterator over key/value pairs.
  using iterator = Storage::iterator;
  /// Const iterator over key/value pairs.
  using const_iterator = Storage::const_iterator;

  /// Construct an empty JSON object.
  Object() = default;
  // KV is a trivial key-value struct for list-initialization.
  // (using std::pair forces extra copies).
  struct KV;
  /// Construct an Object from initializer-list \p Properties.
  ///
  /// \param Properties Key/value pairs to insert.
  explicit Object(std::initializer_list<KV> Properties);

  /// Return an iterator to the first property.
  ///
  /// \return A mutable iterator to the first property.
  iterator begin() { return M.begin(); }
  /// Return a const iterator to the first property.
  ///
  /// \return A const iterator to the first property.
  const_iterator begin() const { return M.begin(); }
  /// Return an iterator past the last property.
  ///
  /// \return A mutable iterator past the last property.
  iterator end() { return M.end(); }
  /// Return a const iterator past the last property.
  ///
  /// \return A const iterator past the last property.
  const_iterator end() const { return M.end(); }

  /// Return true if the object has no properties.
  ///
  /// \return True if the object is empty.
  bool empty() const { return M.empty(); }
  /// Return the number of properties in the object.
  ///
  /// \return The number of properties.
  size_t size() const { return M.size(); }

  /// Remove all properties from the object.
  void clear() { M.clear(); }
  std::pair<iterator, bool> insert(KV E);
  /// Insert or emplace a property with key \p K.
  ///
  /// \param K Property name.
  /// \param Args Arguments used to construct the value.
  /// \return An iterator to the property and whether insertion occurred.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const ObjectKey &K, Ts &&... Args) {
    return M.try_emplace(K, std::forward<Ts>(Args)...);
  }
  /// Insert or emplace a property with movable key \p K.
  ///
  /// \param K Property name to move from.
  /// \param Args Arguments used to construct the value.
  /// \return An iterator to the property and whether insertion occurred.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(ObjectKey &&K, Ts &&... Args) {
    return M.try_emplace(std::move(K), std::forward<Ts>(Args)...);
  }
  bool erase(StringRef K);
  /// Erase the property at iterator \p I.
  ///
  /// \param I Iterator to the property to erase.
  void erase(iterator I) { M.erase(I); }

  /// Find the property named \p K.
  ///
  /// \param K Property name to look up.
  /// \return An iterator to the property, or end() if not found.
  iterator find(StringRef K) { return M.find_as(K); }
  /// Find the property named \p K.
  ///
  /// \param K Property name to look up.
  /// \return A const iterator to the property, or end() if not found.
  const_iterator find(StringRef K) const { return M.find_as(K); }
  // operator[] acts as if Value was default-constructible as null.
  /// Return a mutable reference to property \p K, inserting null if missing.
  ///
  /// \param K Property name.
  /// \return A mutable reference to the property value.
  LLVM_ABI Value &operator[](const ObjectKey &K);
  /// Return a mutable reference to property \p K, inserting null if missing.
  ///
  /// \param K Property name to move from.
  /// \return A mutable reference to the property value.
  LLVM_ABI Value &operator[](ObjectKey &&K);
  // Look up a property, returning nullptr if it doesn't exist.
  /// Return a pointer to property \p K, or nullptr if missing.
  ///
  /// \param K Property name to look up.
  /// \return A mutable pointer to the value, or nullptr if missing.
  LLVM_ABI Value *get(StringRef K);
  /// Return a const pointer to property \p K, or nullptr if missing.
  ///
  /// \param K Property name to look up.
  /// \return A const pointer to the value, or nullptr if missing.
  LLVM_ABI const Value *get(StringRef K) const;
  // Typed accessors return std::nullopt/nullptr if
  //   - the property doesn't exist
  //   - or it has the wrong type
  /// Return property \p K as null if present and null-typed.
  ///
  /// \param K Property name to look up.
  /// \return nullptr if present and null, otherwise nullopt.
  LLVM_ABI std::optional<std::nullptr_t> getNull(StringRef K) const;
  /// Return property \p K as a boolean if present and boolean-typed.
  ///
  /// \param K Property name to look up.
  /// \return The boolean, or nullopt if missing or wrong type.
  LLVM_ABI std::optional<bool> getBoolean(StringRef K) const;
  /// Return property \p K as a number if present and number-typed.
  ///
  /// \param K Property name to look up.
  /// \return The number as a double, or nullopt if missing or wrong type.
  LLVM_ABI std::optional<double> getNumber(StringRef K) const;
  /// Return property \p K as an integer if present and integer-typed.
  ///
  /// \param K Property name to look up.
  /// \return The integer, or nullopt if missing or wrong type.
  LLVM_ABI std::optional<int64_t> getInteger(StringRef K) const;
  /// Return property \p K as a string if present and string-typed.
  ///
  /// \param K Property name to look up.
  /// \return The string, or nullopt if missing or wrong type.
  LLVM_ABI std::optional<llvm::StringRef> getString(StringRef K) const;
  /// Return property \p K as an Object if present and object-typed.
  ///
  /// \param K Property name to look up.
  /// \return A const Object pointer, or nullptr if missing or wrong type.
  LLVM_ABI const json::Object *getObject(StringRef K) const;
  /// Return property \p K as a mutable Object if present and object-typed.
  ///
  /// \param K Property name to look up.
  /// \return A mutable Object pointer, or nullptr if missing or wrong type.
  LLVM_ABI json::Object *getObject(StringRef K);
  /// Return property \p K as an Array if present and array-typed.
  ///
  /// \param K Property name to look up.
  /// \return A const Array pointer, or nullptr if missing or wrong type.
  LLVM_ABI const json::Array *getArray(StringRef K) const;
  /// Return property \p K as a mutable Array if present and array-typed.
  ///
  /// \param K Property name to look up.
  /// \return A mutable Array pointer, or nullptr if missing or wrong type.
  LLVM_ABI json::Array *getArray(StringRef K);

  friend LLVM_ABI bool operator==(const Object &LHS, const Object &RHS);
};
/// Return true if objects \p LHS and \p RHS have equal properties.
///
/// \param LHS Left-hand object.
/// \param RHS Right-hand object.
/// \return True if \p LHS and \p RHS have equal properties.
LLVM_ABI bool operator==(const Object &LHS, const Object &RHS);
/// Return true if objects \p LHS and \p RHS differ.
///
/// \param LHS Left-hand object.
/// \param RHS Right-hand object.
/// \return True if \p LHS and \p RHS are not equal.
inline bool operator!=(const Object &LHS, const Object &RHS) {
  return !(LHS == RHS);
}

/// An Array is a JSON array, which contains heterogeneous JSON values.
/// It simulates std::vector<Value>.
class Array {
  std::vector<Value> V;

public:
  /// Element type stored in the array.
  using value_type = Value;
  /// Mutable iterator over array elements.
  using iterator = std::vector<Value>::iterator;
  /// Const iterator over array elements.
  using const_iterator = std::vector<Value>::const_iterator;

  /// Construct an empty JSON array.
  Array() = default;
  /// Construct an Array from initializer-list \p Elements.
  ///
  /// \param Elements Array elements.
  LLVM_ABI explicit Array(std::initializer_list<Value> Elements);
  /// Construct an Array by converting each element of collection \p C.
  ///
  /// \param C Collection whose elements convert to Value.
  template <typename Collection> explicit Array(const Collection &C) {
    for (const auto &V : C)
      emplace_back(V);
  }

  Value &operator[](size_t I);
  const Value &operator[](size_t I) const;
  Value &front();
  const Value &front() const;
  Value &back();
  const Value &back() const;
  Value *data();
  const Value *data() const;

  iterator begin();
  const_iterator begin() const;
  iterator end();
  const_iterator end() const;

  bool empty() const;
  size_t size() const;
  void reserve(size_t S);

  void clear();
  void push_back(const Value &E);
  void push_back(Value &&E);
  template <typename... Args> void emplace_back(Args &&...A);
  void pop_back();
  iterator insert(const_iterator P, const Value &E);
  iterator insert(const_iterator P, Value &&E);
  template <typename It> iterator insert(const_iterator P, It A, It Z);
  template <typename... Args> iterator emplace(const_iterator P, Args &&...A);
  iterator erase(const_iterator P);

  friend bool operator==(const Array &L, const Array &R);
};
/// Return true if arrays \p L and \p R are not equal.
///
/// \param L Left-hand array.
/// \param R Right-hand array.
/// \return True if \p L and \p R are not equal.
inline bool operator!=(const Array &L, const Array &R) { return !(L == R); }

/// A Value is an JSON value of unknown type.
/// They can be copied, but should generally be moved.
///
/// === Composing values ===
///
/// You can implicitly construct Values from:
///   - strings: std::string, SmallString, formatv, StringRef, char*
///              (char*, and StringRef are references, not copies!)
///   - numbers
///   - booleans
///   - null: nullptr
///   - arrays: {"foo", 42.0, false}
///   - serializable things: types with toJSON(const T&)->Value, found by ADL
///
/// They can also be constructed from object/array helpers:
///   - json::Object is a type like map<ObjectKey, Value>
///   - json::Array is a type like vector<Value>
/// These can be list-initialized, or used to build up collections in a loop.
/// json::ary(Collection) converts all items in a collection to Values.
///
/// === Inspecting values ===
///
/// Each Value is one of the JSON kinds:
///   null    (nullptr_t)
///   boolean (bool)
///   number  (double, int64 or uint64)
///   string  (StringRef)
///   array   (json::Array)
///   object  (json::Object)
///
/// The kind can be queried directly, or implicitly via the typed accessors:
///   if (std::optional<StringRef> S = E.getAsString()
///     assert(E.kind() == Value::String);
///
/// Array and Object also have typed indexing accessors for easy traversal:
///   Expected<Value> E = parse(R"( {"options": {"font": "sans-serif"}} )");
///   if (Object* O = E->getAsObject())
///     if (Object* Opts = O->getObject("options"))
///       if (std::optional<StringRef> Font = Opts->getString("font"))
///         assert(Opts->at("font").kind() == Value::String);
///
/// === Converting JSON values to C++ types ===
///
/// The convention is to have a deserializer function findable via ADL:
///     fromJSON(const json::Value&, T&, Path) -> bool
///
/// The return value indicates overall success, and Path is used for precise
/// error reporting. (The Path::Root passed in at the top level fromJSON call
/// captures any nested error and can render it in context).
/// If conversion fails, fromJSON calls Path::report() and immediately returns.
/// This ensures that the first fatal error survives.
///
/// Deserializers are provided for:
///   - bool
///   - int and int64_t
///   - double
///   - std::string
///   - vector<T>, where T is deserializable
///   - map<string, T>, where T is deserializable
///   - std::optional<T>, where T is deserializable
/// ObjectMapper can help writing fromJSON() functions for object types.
///
/// For conversion in the other direction, the serializer function is:
///    toJSON(const T&) -> json::Value
/// If this exists, then it also allows constructing Value from T, and can
/// be used to serialize vector<T>, map<string, T>, and std::optional<T>.
///
/// === Serialization ===
///
/// Values can be serialized to JSON:
///   1) raw_ostream << Value                    // Basic formatting.
///   2) raw_ostream << formatv("{0}", Value)    // Basic formatting.
///   3) raw_ostream << formatv("{0:2}", Value)  // Pretty-print with indent 2.
///
/// And parsed:
///   Expected<Value> E = json::parse("[1, 2, null]");
///   assert(E && E->kind() == Value::Array);
class Value {
public:
  /// Discriminator for the dynamic JSON type stored in a Value.
  enum Kind {
    /// JSON null.
    Null,
    /// JSON boolean.
    Boolean,
    /// JSON number (int64, uint64, or double at full precision).
    ///
    /// Number values can store both int64s and doubles at full precision,
    /// depending on what they were constructed/parsed from.
    Number,
    /// JSON string.
    String,
    /// JSON array.
    Array,
    /// JSON object.
    Object,
  };

  // It would be nice to have Value() be null. But that would make {} null too.
  /// Copy-construct a Value from \p M.
  ///
  /// \param M Value to copy.
  Value(const Value &M) { copyFrom(M); }
  /// Move-construct a Value from \p M.
  ///
  /// \param M Value to move from.
  Value(Value &&M) { moveFrom(std::move(M)); }
  /// Construct an array Value from initializer-list \p Elements.
  ///
  /// \param Elements Array elements.
  LLVM_ABI Value(std::initializer_list<Value> Elements);
  /// Construct a Value that takes ownership of array \p Elements.
  ///
  /// \param Elements Array to move from.
  Value(json::Array &&Elements) : Type(T_Array) {
    create<json::Array>(std::move(Elements));
  }
  /// Construct an array Value by converting each element of \p C.
  ///
  /// \param C Collection whose elements convert to Value.
  template <typename Elt>
  Value(const std::vector<Elt> &C) : Value(json::Array(C)) {}
  /// Construct a Value that takes ownership of object \p Properties.
  ///
  /// \param Properties Object to move from.
  Value(json::Object &&Properties) : Type(T_Object) {
    create<json::Object>(std::move(Properties));
  }
  /// Construct an object Value by converting each element of map \p C.
  ///
  /// \param C Map whose values convert to Value.
  template <typename Elt>
  Value(const std::map<std::string, Elt> &C) : Value(json::Object(C)) {}
  // Strings: types with value semantics. Must be valid UTF-8.
  /// Construct a string Value that owns \p V.
  ///
  /// \param V UTF-8 string to take ownership of.
  Value(std::string V) : Type(T_String) {
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      V = fixUTF8(V);
    }
    create<std::string>(std::move(V));
  }
  /// Construct a string Value from character buffer \p V.
  ///
  /// \param V Characters forming a UTF-8 string.
  Value(const llvm::SmallVectorImpl<char> &V)
      : Value(std::string(V.begin(), V.end())) {}
  /// Construct a string Value from formatv object \p V.
  ///
  /// \param V Formatted string source.
  Value(const llvm::formatv_object_base &V) : Value(V.str()) {}
  // Strings: types with reference semantics. Must be valid UTF-8.
  /// Construct a string Value referencing \p V.
  ///
  /// \param V UTF-8 string view (referenced, not copied).
  Value(StringRef V) : Type(T_StringRef) {
    create<llvm::StringRef>(V);
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = Value(fixUTF8(V));
    }
  }
  /// Construct a string Value from NUL-terminated string \p V.
  ///
  /// \param V UTF-8 C string (referenced, not copied).
  Value(const char *V) : Value(StringRef(V)) {}
  /// Construct a null JSON Value.
  ///
  /// \param Null Ignored; only the nullptr_t type selects this constructor.
  Value(std::nullptr_t Null) : Type(T_Null) {}
  // Boolean (disallow implicit conversions).
  // (The last template parameter is a dummy to keep templates distinct.)
  /// Construct a boolean Value from \p B.
  ///
  /// \param B Boolean value (only the bool type is accepted).
  template <typename T, typename = std::enable_if_t<std::is_same_v<T, bool>>,
            bool = false>
  Value(T B) : Type(T_Boolean) {
    create<bool>(B);
  }

  // Unsigned 64-bit integers.
  /// Construct a number Value from unsigned 64-bit integer \p V.
  ///
  /// \param V Unsigned integer exactly 64 bits wide.
  template <typename T, typename = std::enable_if_t<is_uint_64_bit_v<T>>>
  Value(T V) : Type(T_UINT64) {
    create<uint64_t>(uint64_t{V});
  }

  // Integers (except boolean and uint64_t).
  // Must be non-narrowing convertible to int64_t.
  /// Construct a number Value from integer \p I.
  ///
  /// \param I Integer non-narrowing convertible to int64_t.
  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>,
            typename = std::enable_if_t<!std::is_same_v<T, bool>>,
            typename = std::enable_if_t<!is_uint_64_bit_v<T>>>
  Value(T I) : Type(T_Integer) {
    create<int64_t>(int64_t{I});
  }
  // Floating point. Must be non-narrowing convertible to double.
  /// Construct a number Value from floating-point \p D.
  ///
  /// \param D Floating-point value non-narrowing convertible to double.
  template <typename T,
            typename = std::enable_if_t<std::is_floating_point_v<T>>,
            double * = nullptr>
  Value(T D) : Type(T_Double) {
    create<double>(double{D});
  }
  // Serializable types: with a toJSON(const T&)->Value function, found by ADL.
  /// Construct a Value by serializing \p V with ADL toJSON.
  ///
  /// \param V Value of a type with toJSON(const T&) -> Value.
  template <typename T,
            typename = std::enable_if_t<
                std::is_same_v<Value, decltype(toJSON(*(const T *)nullptr))>>,
            Value * = nullptr>
  Value(const T &V) : Value(toJSON(V)) {}

  /// Copy-assign from \p M.
  ///
  /// \param M Value to copy.
  /// \return A reference to this Value.
  Value &operator=(const Value &M) {
    destroy();
    copyFrom(M);
    return *this;
  }
  /// Move-assign from \p M.
  ///
  /// \param M Value to move from.
  /// \return A reference to this Value.
  Value &operator=(Value &&M) {
    destroy();
    moveFrom(std::move(M));
    return *this;
  }
  /// Destroy this Value and free owned storage.
  ~Value() { destroy(); }

  /// Return the JSON kind stored in this Value.
  ///
  /// \return The Kind discriminator for this Value.
  Kind kind() const {
    switch (Type) {
    case T_Null:
      return Null;
    case T_Boolean:
      return Boolean;
    case T_Double:
    case T_Integer:
    case T_UINT64:
      return Number;
    case T_String:
    case T_StringRef:
      return String;
    case T_Object:
      return Object;
    case T_Array:
      return Array;
    }
    llvm_unreachable("Unknown kind");
  }

  // Typed accessors return std::nullopt/nullptr if the Value is not of this
  // type.
  /// Return null if this Value is null, otherwise nullopt.
  ///
  /// \return nullptr if this Value is null, otherwise nullopt.
  std::optional<std::nullptr_t> getAsNull() const {
    if (LLVM_LIKELY(Type == T_Null))
      return nullptr;
    return std::nullopt;
  }
  /// Return the boolean if this Value is a boolean, otherwise nullopt.
  ///
  /// \return The boolean value, or nullopt if not a boolean.
  std::optional<bool> getAsBoolean() const {
    if (LLVM_LIKELY(Type == T_Boolean))
      return as<bool>();
    return std::nullopt;
  }
  /// Return this Value as a double if it is a number, otherwise nullopt.
  ///
  /// \return The number as a double, or nullopt if not a number.
  std::optional<double> getAsNumber() const {
    if (LLVM_LIKELY(Type == T_Double))
      return as<double>();
    if (LLVM_LIKELY(Type == T_Integer))
      return as<int64_t>();
    if (LLVM_LIKELY(Type == T_UINT64))
      return as<uint64_t>();
    return std::nullopt;
  }
  // Succeeds if the Value is a Number, and exactly representable as int64_t.
  /// Return this Value as int64_t if it is an exactly representable number.
  ///
  /// \return The integer, or nullopt if not exactly representable as int64_t.
  std::optional<int64_t> getAsInteger() const {
    if (LLVM_LIKELY(Type == T_Integer))
      return as<int64_t>();
    if (LLVM_LIKELY(Type == T_UINT64)) {
      uint64_t U = as<uint64_t>();
      if (LLVM_LIKELY(U <= uint64_t(std::numeric_limits<int64_t>::max()))) {
        return U;
      }
    }
    if (LLVM_LIKELY(Type == T_Double)) {
      double D = as<double>();
      if (LLVM_LIKELY(std::modf(D, &D) == 0.0 &&
                      D >= double(std::numeric_limits<int64_t>::min()) &&
                      D <= double(std::numeric_limits<int64_t>::max())))
        return D;
    }
    return std::nullopt;
  }
  /// Return this Value as uint64_t if it is a non-negative integer.
  ///
  /// \return The unsigned integer, or nullopt if not a non-negative integer.
  std::optional<uint64_t> getAsUINT64() const {
    if (Type == T_UINT64)
      return as<uint64_t>();
    else if (Type == T_Integer) {
      int64_t N = as<int64_t>();
      if (N >= 0)
        return as<uint64_t>();
    }
    return std::nullopt;
  }
  /// Return this Value as a string if it is a string, otherwise nullopt.
  ///
  /// \return The string contents, or nullopt if not a string.
  std::optional<llvm::StringRef> getAsString() const {
    if (Type == T_String)
      return llvm::StringRef(as<std::string>());
    if (LLVM_LIKELY(Type == T_StringRef))
      return as<llvm::StringRef>();
    return std::nullopt;
  }
  /// Return this Value as an Object pointer, or nullptr if not an object.
  ///
  /// \return A const Object pointer, or nullptr if not an object.
  const json::Object *getAsObject() const {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  /// Return this Value as a mutable Object pointer, or nullptr if not an object.
  ///
  /// \return A mutable Object pointer, or nullptr if not an object.
  json::Object *getAsObject() {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  /// Return this Value as an Array pointer, or nullptr if not an array.
  ///
  /// \return A const Array pointer, or nullptr if not an array.
  const json::Array *getAsArray() const {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }
  /// Return this Value as a mutable Array pointer, or nullptr if not an array.
  ///
  /// \return A mutable Array pointer, or nullptr if not an array.
  json::Array *getAsArray() {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }

  /// Print this Value as JSON to \p OS.
  ///
  /// \param OS Stream to write JSON to.
  LLVM_ABI void print(llvm::raw_ostream &OS) const;
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this Value to stderr for debugging.
  LLVM_DUMP_METHOD void dump() const;
#endif // !NDEBUG || LLVM_ENABLE_DUMP

private:
  LLVM_ABI void destroy();
  LLVM_ABI void copyFrom(const Value &M);
  // We allow moving from *const* Values, by marking all members as mutable!
  // This hack is needed to support initializer-list syntax efficiently.
  // (std::initializer_list<T> is a container of const T).
  LLVM_ABI void moveFrom(const Value &&M);
  friend class Array;
  friend class Object;

  template <typename T, typename... U> void create(U &&...V) {
    new (reinterpret_cast<T *>(&Union)) T(std::forward<U>(V)...);
  }
  template <typename T> T &as() const {
    // Using this two-step static_cast via void * instead of reinterpret_cast
    // silences a -Wstrict-aliasing false positive from GCC6 and earlier.
    void *Storage = static_cast<void *>(&Union);
    return *static_cast<T *>(Storage);
  }

  friend class OStream;

  enum ValueType : char16_t {
    T_Null,
    T_Boolean,
    T_Double,
    T_Integer,
    T_UINT64,
    T_StringRef,
    T_String,
    T_Object,
    T_Array,
  };
  // All members mutable, see moveFrom().
  mutable ValueType Type;
  mutable llvm::AlignedCharArrayUnion<bool, double, int64_t, uint64_t,
                                      llvm::StringRef, std::string, json::Array,
                                      json::Object>
      Union;
  LLVM_ABI friend bool operator==(const Value &LHS, const Value &RHS);
};

/// Return true if values \p LHS and \p RHS are equal.
///
/// \param LHS Left-hand value.
/// \param RHS Right-hand value.
/// \return True if \p LHS and \p RHS represent equal JSON.
LLVM_ABI bool operator==(const Value &LHS, const Value &RHS);
/// Return true if values \p L and \p R are not equal.
///
/// \param L Left-hand value.
/// \param R Right-hand value.
/// \return True if \p L and \p R are not equal.
inline bool operator!=(const Value &L, const Value &R) { return !(L == R); }

// Array Methods
/// Return a mutable reference to the element at index \p I.
///
/// \param I Zero-based element index.
/// \return A mutable reference to the element at \p I.
inline Value &Array::operator[](size_t I) { return V[I]; }
/// Return a const reference to the element at index \p I.
///
/// \param I Zero-based element index.
/// \return A const reference to the element at \p I.
inline const Value &Array::operator[](size_t I) const { return V[I]; }
/// Return a mutable reference to the first element.
///
/// \return A mutable reference to the first element.
inline Value &Array::front() { return V.front(); }
/// Return a const reference to the first element.
///
/// \return A const reference to the first element.
inline const Value &Array::front() const { return V.front(); }
/// Return a mutable reference to the last element.
///
/// \return A mutable reference to the last element.
inline Value &Array::back() { return V.back(); }
/// Return a const reference to the last element.
///
/// \return A const reference to the last element.
inline const Value &Array::back() const { return V.back(); }
/// Return a pointer to the first element, or nullptr if empty.
///
/// \return A pointer to the first element, or nullptr if empty.
inline Value *Array::data() { return V.data(); }
/// Return a const pointer to the first element, or nullptr if empty.
///
/// \return A const pointer to the first element, or nullptr if empty.
inline const Value *Array::data() const { return V.data(); }

/// Return an iterator to the first element.
///
/// \return A mutable iterator to the first element.
inline Array::iterator Array::begin() { return V.begin(); }
/// Return a const iterator to the first element.
///
/// \return A const iterator to the first element.
inline Array::const_iterator Array::begin() const { return V.begin(); }
/// Return an iterator past the last element.
///
/// \return A mutable iterator past the last element.
inline Array::iterator Array::end() { return V.end(); }
/// Return a const iterator past the last element.
///
/// \return A const iterator past the last element.
inline Array::const_iterator Array::end() const { return V.end(); }

/// Return true if the array contains no elements.
///
/// \return True if the array is empty.
inline bool Array::empty() const { return V.empty(); }
/// Return the number of elements in the array.
///
/// \return The number of elements.
inline size_t Array::size() const { return V.size(); }
/// Reserve storage for at least \p S elements.
///
/// \param S Minimum capacity to reserve.
inline void Array::reserve(size_t S) { V.reserve(S); }

/// Remove all elements from the array.
inline void Array::clear() { V.clear(); }
/// Append a copy of \p E to the array.
///
/// \param E Value to append.
inline void Array::push_back(const Value &E) { V.push_back(E); }
/// Append \p E to the array by move.
///
/// \param E Value to append.
inline void Array::push_back(Value &&E) { V.push_back(std::move(E)); }
/// Append a newly constructed element to the array.
///
/// \param A Arguments used to construct the element.
template <typename... Args> inline void Array::emplace_back(Args &&...A) {
  V.emplace_back(std::forward<Args>(A)...);
}
/// Remove the last element from the array.
inline void Array::pop_back() { V.pop_back(); }
/// Insert a copy of \p E before iterator \p P.
///
/// \param P Insertion position.
/// \param E Value to insert.
/// \return An iterator to the newly inserted element.
inline Array::iterator Array::insert(const_iterator P, const Value &E) {
  return V.insert(P, E);
}
/// Insert \p E by move before iterator \p P.
///
/// \param P Insertion position.
/// \param E Value to insert.
/// \return An iterator to the newly inserted element.
inline Array::iterator Array::insert(const_iterator P, Value &&E) {
  return V.insert(P, std::move(E));
}
/// Insert the range [\p A, \p Z) before iterator \p P.
///
/// \param P Insertion position.
/// \param A Start of the range to insert.
/// \param Z End of the range to insert.
/// \return An iterator to the first inserted element, or \p P if empty.
template <typename It>
inline Array::iterator Array::insert(const_iterator P, It A, It Z) {
  return V.insert(P, A, Z);
}
/// Emplace a newly constructed element before iterator \p P.
///
/// \param P Insertion position.
/// \param A Arguments used to construct the element.
/// \return An iterator to the newly inserted element.
template <typename... Args>
inline Array::iterator Array::emplace(const_iterator P, Args &&...A) {
  return V.emplace(P, std::forward<Args>(A)...);
}
/// Erase the element at iterator \p P.
///
/// \param P Element to erase.
/// \return An iterator to the element following the erased one.
inline Array::iterator Array::erase(const_iterator P) { return V.erase(P); }
/// Return true if arrays \p L and \p R contain equal elements.
///
/// \param L Left-hand array.
/// \param R Right-hand array.
/// \return True if \p L and \p R have the same elements in order.
inline bool operator==(const Array &L, const Array &R) { return L.V == R.V; }

/// UTF-8 string key for JSON objects, optimized for literals.
///
/// ObjectKey is used to capture keys in Object. Like Value but:
///   - only strings are allowed
///   - it's optimized for the string literal case (Owned == nullptr)
/// Like Value, strings must be UTF-8. See isUTF8 documentation for details.
class ObjectKey {
public:
  /// Construct a key from NUL-terminated string \p S.
  ///
  /// \param S UTF-8 C string (referenced, not copied).
  ObjectKey(const char *S) : ObjectKey(StringRef(S)) {}
  /// Construct a key that owns string \p S.
  ///
  /// \param S UTF-8 string to take ownership of.
  ObjectKey(std::string S) : Owned(new std::string(std::move(S))) {
    if (LLVM_UNLIKELY(!isUTF8(*Owned))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *Owned = fixUTF8(*Owned);
    }
    Data = *Owned;
  }
  /// Construct a key referencing string \p S.
  ///
  /// \param S UTF-8 string view (referenced, not copied).
  ObjectKey(llvm::StringRef S) : Data(S) {
    if (LLVM_UNLIKELY(!isUTF8(Data))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = ObjectKey(fixUTF8(S));
    }
  }
  /// Construct a key from character buffer \p V.
  ///
  /// \param V Characters forming a UTF-8 string.
  ObjectKey(const llvm::SmallVectorImpl<char> &V)
      : ObjectKey(std::string(V.begin(), V.end())) {}
  /// Construct a key from formatv object \p V.
  ///
  /// \param V Formatted string source.
  ObjectKey(const llvm::formatv_object_base &V) : ObjectKey(V.str()) {}

  /// Copy-construct a key from \p C.
  ///
  /// \param C Key to copy.
  ObjectKey(const ObjectKey &C) { *this = C; }
  /// Move-construct a key from \p C.
  ///
  /// \param C Key to move from.
  ObjectKey(ObjectKey &&C) : ObjectKey(static_cast<const ObjectKey &&>(C)) {}
  /// Copy-assign from \p C.
  ///
  /// \param C Key to copy.
  /// \return A reference to this key.
  ObjectKey &operator=(const ObjectKey &C) {
    if (C.Owned) {
      Owned.reset(new std::string(*C.Owned));
      Data = *Owned;
    } else {
      Data = C.Data;
    }
    return *this;
  }
  /// Move-assign from \p C.
  ///
  /// \param C Key to move from.
  /// \return A reference to this key.
  ObjectKey &operator=(ObjectKey &&C) = default;

  /// Convert this key to a StringRef.
  ///
  /// \return A StringRef view of this key's UTF-8 bytes.
  operator llvm::StringRef() const { return Data; }
  /// Return an owned std::string copy of this key.
  ///
  /// \return A std::string containing this key's UTF-8 bytes.
  std::string str() const { return Data.str(); }

private:
  // FIXME: this is unneccesarily large (3 pointers). Pointer + length + owned
  // could be 2 pointers at most.
  std::unique_ptr<std::string> Owned;
  llvm::StringRef Data;
};

/// Return true if object keys \p L and \p R compare equal.
///
/// \param L Left-hand key.
/// \param R Right-hand key.
/// \return True if \p L and \p R have the same string contents.
inline bool operator==(const ObjectKey &L, const ObjectKey &R) {
  return llvm::StringRef(L) == llvm::StringRef(R);
}
/// Return true if object keys \p L and \p R differ.
///
/// \param L Left-hand key.
/// \param R Right-hand key.
/// \return True if \p L and \p R are not equal.
inline bool operator!=(const ObjectKey &L, const ObjectKey &R) {
  return !(L == R);
}
/// Return true if key \p L is lexicographically less than \p R.
///
/// \param L Left-hand key.
/// \param R Right-hand key.
/// \return True if \p L compares less than \p R as strings.
inline bool operator<(const ObjectKey &L, const ObjectKey &R) {
  return StringRef(L) < StringRef(R);
}

/// Key/value pair used to list-initialize Object.
struct Object::KV {
  /// Object property name.
  ObjectKey K;
  /// Object property value.
  Value V;
};

/// Construct an Object from initializer-list \p Properties.
///
/// \param Properties Key/value pairs to insert.
inline Object::Object(std::initializer_list<KV> Properties) {
  for (const auto &P : Properties) {
    auto R = try_emplace(P.K, nullptr);
    if (R.second)
      R.first->getSecond().moveFrom(std::move(P.V));
  }
}
/// Insert key/value pair \p E if the key is not already present.
///
/// \param E Pair to insert.
/// \return An iterator to the property and whether insertion occurred.
inline std::pair<Object::iterator, bool> Object::insert(KV E) {
  return try_emplace(std::move(E.K), std::move(E.V));
}
/// Erase the property named \p K if present.
///
/// \param K Property name to erase.
/// \return True if a property named \p K was erased.
inline bool Object::erase(StringRef K) {
  return M.erase(ObjectKey(K));
}

/// Return pointers to \p O's elements sorted by key.
///
/// \param O Object whose elements are sorted.
/// \return Pointers to \p O's key/value pairs in key order.
LLVM_ABI std::vector<const Object::value_type *>
sortedElements(const Object &O);

/// Path cursor marking a position within a JSON Value tree.
///
/// The Value is a tree, and this is the path from the root to the current node.
/// This is used to associate errors with particular subobjects.
class Path {
public:
  class Root;

  /// Record that the value at this path is invalid.
  ///
  /// Message is e.g. "expected number" and becomes part of the final error.
  /// This overwrites any previously written error message in the root.
  ///
  /// \param Message Description of why the value is invalid.
  LLVM_ABI void report(llvm::StringLiteral Message);

  /// Construct a path at the root \p R.
  ///
  /// \param R Root that owns error state for this path.
  Path(Root &R) : Parent(nullptr), Seg(&R) {}
  /// Derive a path for array element this[\p Index].
  ///
  /// \param Index Zero-based array index.
  /// \return A path pointing at the indexed array element.
  Path index(unsigned Index) const { return Path(this, Segment(Index)); }
  /// Derive a path for object field this.\p Field.
  ///
  /// \param Field Object property name.
  /// \return A path pointing at the named object field.
  Path field(StringRef Field) const { return Path(this, Segment(Field)); }

private:
  /// One element in a JSON path: an object field (.foo) or array index [27].
  /// Exception: the root Path encodes a pointer to the Path::Root.
  class Segment {
    uintptr_t Pointer;
    unsigned Offset;

  public:
    Segment() = default;
    Segment(Root *R) : Pointer(reinterpret_cast<uintptr_t>(R)) {}
    Segment(llvm::StringRef Field)
        : Pointer(reinterpret_cast<uintptr_t>(Field.data())),
          Offset(static_cast<unsigned>(Field.size())) {}
    Segment(unsigned Index) : Pointer(0), Offset(Index) {}

    bool isField() const { return Pointer != 0; }
    StringRef field() const {
      return StringRef(reinterpret_cast<const char *>(Pointer), Offset);
    }
    unsigned index() const { return Offset; }
    Root *root() const { return reinterpret_cast<Root *>(Pointer); }
  };

  const Path *Parent;
  Segment Seg;

  Path(const Path *Parent, Segment S) : Parent(Parent), Seg(S) {}
};

/// The root is the trivial Path to the root value.
/// It also stores the latest reported error and the path where it occurred.
class Path::Root {
  llvm::StringRef Name;
  llvm::StringLiteral ErrorMessage;
  std::vector<Path::Segment> ErrorPath; // Only valid in error state. Reversed.

  LLVM_ABI friend void Path::report(llvm::StringLiteral Message);

public:
  /// Construct a root path with optional display name \p Name.
  ///
  /// \param Name Name of the root value used in error messages.
  Root(llvm::StringRef Name = "") : Name(Name), ErrorMessage("") {}
  // No copy/move allowed as there are incoming pointers.
  /// Roots are not move-constructible.
  ///
  /// \param Other Unused; move construction is deleted.
  Root(Root &&Other) = delete;
  /// Roots are not move-assignable.
  ///
  /// \param Other Unused; move assignment is deleted.
  Root &operator=(Root &&Other) = delete;
  /// Roots are not copy-constructible.
  ///
  /// \param Other Unused; copy construction is deleted.
  Root(const Root &Other) = delete;
  /// Roots are not copy-assignable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  Root &operator=(const Root &Other) = delete;

  /// Returns the last error reported, or else a generic error.
  ///
  /// \return An Error describing the last reported path failure.
  LLVM_ABI Error getError() const;
  /// Print the root value with the error shown inline as a comment.
  ///
  /// Unrelated parts of the value are elided for brevity, e.g.
  ///   {
  ///      "id": 42,
  ///      "name": /* expected string */ null,
  ///      "properties": { ... }
  ///   }
  ///
  /// \param V Root JSON value to print.
  /// \param OS Stream to write the annotated value to.
  LLVM_ABI void printErrorContext(const Value &V,
                                  llvm::raw_ostream &OS) const;
};

// Standard deserializers are provided for primitive types.
// See comments on Value.
/// Deserialize a JSON string into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination string.
/// \param P Path used to report type errors.
/// \return True if \p E was a JSON string.
inline bool fromJSON(const Value &E, std::string &Out, Path P) {
  if (auto S = E.getAsString()) {
    Out = std::string(*S);
    return true;
  }
  P.report("expected string");
  return false;
}
/// Deserialize a JSON integer into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination int.
/// \param P Path used to report type errors.
/// \return True if \p E was an exactly representable integer.
inline bool fromJSON(const Value &E, int &Out, Path P) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  P.report("expected integer");
  return false;
}
/// Deserialize a JSON integer into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination int64_t.
/// \param P Path used to report type errors.
/// \return True if \p E was an exactly representable integer.
inline bool fromJSON(const Value &E, int64_t &Out, Path P) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  P.report("expected integer");
  return false;
}
/// Deserialize a JSON number into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination double.
/// \param P Path used to report type errors.
/// \return True if \p E was a JSON number.
inline bool fromJSON(const Value &E, double &Out, Path P) {
  if (auto S = E.getAsNumber()) {
    Out = *S;
    return true;
  }
  P.report("expected number");
  return false;
}
/// Deserialize a JSON boolean into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination bool.
/// \param P Path used to report type errors.
/// \return True if \p E was a JSON boolean.
inline bool fromJSON(const Value &E, bool &Out, Path P) {
  if (auto S = E.getAsBoolean()) {
    Out = *S;
    return true;
  }
  P.report("expected boolean");
  return false;
}
/// Deserialize a JSON unsigned integer into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination unsigned int.
/// \param P Path used to report type errors.
/// \return True if \p E was an integer convertible to unsigned int.
inline bool fromJSON(const Value &E, unsigned int &Out, Path P) {
  if (auto S = E.getAsInteger()) {
    Out = *S;
    return true;
  }
  P.report("expected unsigned integer");
  return false;
}
/// Deserialize a JSON uint64 into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination uint64_t.
/// \param P Path used to report type errors.
/// \return True if \p E was a non-negative integer fitting uint64_t.
inline bool fromJSON(const Value &E, uint64_t &Out, Path P) {
  if (auto S = E.getAsUINT64()) {
    Out = *S;
    return true;
  }
  P.report("expected uint64_t");
  return false;
}
/// Deserialize a JSON null into \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination nullptr_t.
/// \param P Path used to report type errors.
/// \return True if \p E was JSON null.
inline bool fromJSON(const Value &E, std::nullptr_t &Out, Path P) {
  if (auto S = E.getAsNull()) {
    Out = *S;
    return true;
  }
  P.report("expected null");
  return false;
}
/// Deserialize a JSON value into optional \p Out.
///
/// \param E JSON value to read; null clears the optional.
/// \param Out Destination optional.
/// \param P Path used to report type errors.
/// \return True if \p E was null or successfully deserialized into \c T.
template <typename T>
bool fromJSON(const Value &E, std::optional<T> &Out, Path P) {
  if (E.getAsNull()) {
    Out = std::nullopt;
    return true;
  }
  T Result{};
  if (!fromJSON(E, Result, P))
    return false;
  Out = std::move(Result);
  return true;
}
/// Deserialize a JSON array into vector \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination vector; cleared then filled on success.
/// \param P Path used to report type errors.
/// \return True if \p E was an array and every element deserialized.
template <typename T>
bool fromJSON(const Value &E, std::vector<T> &Out, Path P) {
  if (auto *A = E.getAsArray()) {
    Out.clear();
    Out.resize(A->size());
    for (size_t I = 0; I < A->size(); ++I)
      if (!fromJSON((*A)[I], Out[I], P.index(I)))
        return false;
    return true;
  }
  P.report("expected array");
  return false;
}
/// Deserialize a JSON object into map \p Out.
///
/// \param E JSON value to read.
/// \param Out Destination map; cleared then filled on success.
/// \param P Path used to report type errors.
/// \return True if \p E was an object and every value deserialized.
template <typename T>
bool fromJSON(const Value &E, std::map<std::string, T> &Out, Path P) {
  if (auto *O = E.getAsObject()) {
    Out.clear();
    for (const auto &KV : *O)
      if (!fromJSON(KV.second, Out[std::string(llvm::StringRef(KV.first))],
                    P.field(KV.first)))
        return false;
    return true;
  }
  P.report("expected object");
  return false;
}

// Allow serialization of std::optional<T> for supported T.
/// Serialize optional \p Opt to a JSON value or null.
///
/// \param Opt Optional value to serialize.
/// \return The serialized value of \c *Opt, or JSON null if empty.
template <typename T> Value toJSON(const std::optional<T> &Opt) {
  return Opt ? Value(*Opt) : Value(nullptr);
}

/// Helper for mapping JSON objects onto protocol structs.
///
/// Example:
/// \code
///   bool fromJSON(const Value &E, MyStruct &R, Path P) {
///     ObjectMapper O(E, P);
///     // When returning false, error details were already reported.
///     return O && O.map("mandatory_field", R.MandatoryField) &&
///         O.mapOptional("optional_field", R.OptionalField);
///   }
/// \endcode
class ObjectMapper {
public:
  /// Construct a mapper for object value \p E, reporting on \p P.
  ///
  /// If \p E is not an object, this mapper is invalid and an error is reported.
  ///
  /// \param E JSON value expected to be an object.
  /// \param P Path used to report mapping errors.
  ObjectMapper(const Value &E, Path P) : O(E.getAsObject()), P(P) {
    if (!O)
      P.report("expected object");
  }

  /// True if the expression is an object.
  /// Must be checked before calling map().
  ///
  /// \return True if the mapped value was a JSON object.
  operator bool() const { return O; }

  /// Maps a property to a field.
  ///
  /// If the property is missing or invalid, reports an error.
  ///
  /// \param Prop Property name to look up.
  /// \param Out Destination for the deserialized value.
  /// \return True if the property was present and deserialized successfully.
  template <typename T> bool map(StringLiteral Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    P.field(Prop).report("missing value");
    return false;
  }

  /// Maps an optional property to a field.
  ///
  /// If the property exists and is invalid, reports an error.
  /// Optional requires special handling, because missing keys are OK.
  ///
  /// \param Prop Property name to look up.
  /// \param Out Destination; set to nullopt if the property is missing.
  /// \return True on success, including when the property is missing.
  template <typename T> bool map(StringLiteral Prop, std::optional<T> &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    Out = std::nullopt;
    return true;
  }

  /// Maps a property to a field, if it exists.
  ///
  /// If the property exists and is invalid, reports an error.
  /// If the property does not exist, Out is unchanged.
  ///
  /// \param Prop Property name to look up.
  /// \param Out Destination updated only when the property is present.
  /// \return True on success, or if the property is missing.
  template <typename T> bool mapOptional(StringLiteral Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    return true;
  }

private:
  const Object *O;
  Path P;
};

/// Parses the provided JSON source, or returns a ParseError.
///
/// The returned Value is self-contained and owns its strings (they do not refer
/// to the original source).
///
/// \param JSON JSON text to parse.
/// \return The parsed Value, or a ParseError on failure.
LLVM_ABI llvm::Expected<Value> parse(llvm::StringRef JSON);

/// Error describing a JSON parse failure with source location.
class LLVM_ABI ParseError : public llvm::ErrorInfo<ParseError> {
  const char *Msg;
  unsigned Line, Column, Offset;

public:
  /// RTTI identifier for ParseError.
  static char ID;
  /// Construct a parse error with message and source location.
  ///
  /// \param Msg Human-readable parse failure message.
  /// \param Line One-based line number of the error.
  /// \param Column One-based column number of the error.
  /// \param Offset Byte offset into the source of the error.
  ParseError(const char *Msg, unsigned Line, unsigned Column, unsigned Offset)
      : Msg(Msg), Line(Line), Column(Column), Offset(Offset) {}
  /// Print this parse error to \p OS.
  ///
  /// \param OS Stream to write the error message to.
  void log(llvm::raw_ostream &OS) const override;
  /// Convert this parse error to a std::error_code.
  ///
  /// \return An inconvertible error code; parse errors are not errno-mapped.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

/// Parse JSON and convert the result to type \c T.
///
/// RootName describes the root object and is used in error messages.
///
/// \param JSON JSON text to parse.
/// \param RootName Name of the root value for error reporting.
/// \return The deserialized value of type \c T, or a parse/mapping error.
template <typename T>
Expected<T> parse(const llvm::StringRef &JSON, const char *RootName = "") {
  auto V = parse(JSON);
  if (!V)
    return V.takeError();
  Path::Root R(RootName);
  T Result;
  if (fromJSON(*V, Result, R))
    return std::move(Result);
  return R.getError();
}

/// Streaming JSON writer that does not materialize json::Value trees.
///
/// It's faster, lower-level, and less safe than OS << json::Value.
/// It also allows emitting more constructs, such as comments.
///
/// Only one "top-level" object can be written to a stream.
/// Simplest usage involves passing lambdas (Blocks) to fill in containers:
///
///   json::OStream J(OS);
///   J.array([&]{
///     for (const Event &E : Events)
///       J.object([&] {
///         J.attribute("timestamp", int64_t(E.Time));
///         J.attributeArray("participants", [&] {
///           for (const Participant &P : E.Participants)
///             J.value(P.toString());
///         });
///       });
///   });
///
/// This would produce JSON like:
///
///   [
///     {
///       "timestamp": 19287398741,
///       "participants": [
///         "King Kong",
///         "Miley Cyrus",
///         "Cleopatra"
///       ]
///     },
///     ...
///   ]
///
/// The lower level begin/end methods (arrayBegin()) are more flexible but
/// care must be taken to pair them correctly:
///
///   json::OStream J(OS);
///   J.arrayBegin();
///   for (const Event &E : Events) {
///     J.objectBegin();
///     J.attribute("timestamp", int64_t(E.Time));
///     J.attributeBegin("participants");
///     for (const Participant &P : E.Participants)
///       J.value(P.toString());
///     J.attributeEnd();
///     J.objectEnd();
///   }
///   J.arrayEnd();
///
/// If the call sequence isn't valid JSON, asserts will fire in debug mode.
/// This can be mismatched begin()/end() pairs, trying to emit attributes inside
/// an array, and so on.
/// With asserts disabled, this is undefined behavior.
class OStream {
 public:
  /// Callback that emits nested JSON content between begin/end calls.
  using Block = llvm::function_ref<void()>;
  /// Construct a writer on \p OS, pretty-printing when \p IndentSize is nonzero.
  ///
  /// \param OS Stream to write JSON to.
  /// \param IndentSize Spaces per indent level, or 0 for compact output.
  explicit OStream(llvm::raw_ostream &OS, unsigned IndentSize = 0)
      : OS(OS), IndentSize(IndentSize) {
    Stack.emplace_back();
  }
  /// Destroy the stream, asserting that begin/end calls were matched.
  ~OStream() {
    assert(Stack.size() == 1 && "Unmatched begin()/end()");
    assert(Stack.back().Ctx == Singleton);
    assert(Stack.back().HasValue && "Did not write top-level value");
  }

  /// Flushes the underlying ostream. OStream does not buffer internally.
  void flush() { OS.flush(); }

  // High level functions to output a value.
  // Valid at top-level (exactly once), in an attribute value (exactly once),
  // or in an array (any number of times).

  /// Emit a self-contained value (number, string, vector<string> etc).
  ///
  /// \param V Value to serialize.
  LLVM_ABI void value(const Value &V);
  /// Emit an array whose elements are emitted in the provided Block.
  ///
  /// \param Contents Callback that writes array elements.
  void array(Block Contents) {
    arrayBegin();
    Contents();
    arrayEnd();
  }
  /// Emit an object whose elements are emitted in the provided Block.
  ///
  /// \param Contents Callback that writes object attributes.
  void object(Block Contents) {
    objectBegin();
    Contents();
    objectEnd();
  }
  /// Emit an externally-serialized JSON value.
  ///
  /// The caller must write exactly one valid JSON value to the provided stream.
  /// No validation or formatting of this value occurs.
  ///
  /// \param Contents Callback that writes raw JSON to the stream.
  void rawValue(llvm::function_ref<void(raw_ostream &)> Contents) {
    rawValueBegin();
    Contents(OS);
    rawValueEnd();
  }
  /// Emit \p Contents as a raw, externally-serialized JSON value.
  ///
  /// \param Contents Exact JSON text for a single value.
  void rawValue(llvm::StringRef Contents) {
    rawValue([&](raw_ostream &OS) { OS << Contents; });
  }
  /// Emit a JavaScript comment before the next printed value.
  ///
  /// The string must be valid until the next attribute or value is emitted.
  /// Comments are not part of standard JSON, and many parsers reject them!
  ///
  /// \param Text Comment text to emit.
  LLVM_ABI void comment(llvm::StringRef Text);

  // High level functions to output object attributes.
  // Valid only within an object (any number of times).

  /// Emit an attribute whose value is self-contained (number, vector<int> etc).
  ///
  /// \param Key Attribute name.
  /// \param Contents Attribute value.
  void attribute(llvm::StringRef Key, const Value& Contents) {
    attributeImpl(Key, [&] { value(Contents); });
  }
  /// Emit an attribute whose value is an array with elements from the Block.
  ///
  /// \param Key Attribute name.
  /// \param Contents Callback that writes array elements.
  void attributeArray(llvm::StringRef Key, Block Contents) {
    attributeImpl(Key, [&] { array(Contents); });
  }
  /// Emit an attribute whose value is an object with attributes from the Block.
  ///
  /// \param Key Attribute name.
  /// \param Contents Callback that writes nested attributes.
  void attributeObject(llvm::StringRef Key, Block Contents) {
    attributeImpl(Key, [&] { object(Contents); });
  }

  // Low-level begin/end functions to output arrays, objects, and attributes.
  // Must be correctly paired. Allowed contexts are as above.

  /// Begin writing a JSON array.
  LLVM_ABI void arrayBegin();
  /// Finish the current JSON array.
  LLVM_ABI void arrayEnd();
  /// Begin writing a JSON object.
  LLVM_ABI void objectBegin();
  /// Finish the current JSON object.
  LLVM_ABI void objectEnd();
  /// Begin writing an object attribute named \p Key.
  ///
  /// \param Key Attribute name.
  LLVM_ABI void attributeBegin(llvm::StringRef Key);
  /// Finish the current object attribute value.
  LLVM_ABI void attributeEnd();
  /// Begin a raw value and return the stream to write it to.
  ///
  /// \return The underlying stream for writing one raw JSON value.
  LLVM_ABI raw_ostream &rawValueBegin();
  /// Finish the current raw value.
  LLVM_ABI void rawValueEnd();

private:
  void attributeImpl(llvm::StringRef Key, Block Contents) {
    attributeBegin(Key);
    Contents();
    attributeEnd();
  }

  LLVM_ABI void valueBegin();
  LLVM_ABI void flushComment();
  LLVM_ABI void newline();

  enum Context {
    Singleton, // Top level, or object attribute.
    Array,
    Object,
    RawValue, // External code writing a value to OS directly.
  };
  struct State {
    Context Ctx = Singleton;
    bool HasValue = false;
  };
  llvm::SmallVector<State, 16> Stack; // Never empty.
  llvm::StringRef PendingComment;
  llvm::raw_ostream &OS;
  unsigned IndentSize;
  unsigned Indent = 0;
};

/// Serialize \p V to compact JSON on \p OS.
///
/// The formatting is compact (no extra whitespace) and deterministic.
/// For pretty-printing, use the formatv() format_provider below.
///
/// \param OS Stream to write JSON to.
/// \param V Value to serialize.
/// \return \p OS after writing compact JSON for \p V.
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Value &V) {
  OStream(OS).value(V);
  return OS;
}
} // namespace json

/// formatv provider for printing json::Value.
///
/// The default style is basic/compact formatting, like operator<<.
/// A format string like formatv("{0:2}", Value) pretty-prints with indent 2.
template <> struct format_provider<llvm::json::Value> {
  /// Format \p V onto \p OS, using \p Options as an optional indent width.
  ///
  /// \param V JSON value to print.
  /// \param OS Stream to write to.
  /// \param Options Empty for compact output, or a decimal indent size.
  LLVM_ABI static void format(const llvm::json::Value &V, raw_ostream &OS,
                              StringRef Options);
};
} // namespace llvm

#endif
