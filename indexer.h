#pragma once

#include <algorithm>
#include <iostream>
#include <cstdint>
#include <cassert>
#include <fstream>
#include <unordered_map>

#include "libclangmm/clangmm.h"
#include "libclangmm/Utility.h"

#include "bitfield.h"
#include "utils.h"
#include "optional.h"

#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/document.h>

struct IndexedTypeDef;
struct IndexedFuncDef;
struct IndexedVarDef;

using FileId = int64_t;
using namespace std::experimental;

// TODO: Move off of this weird wrapper, use struct with custom wrappers
//       directly.
BEGIN_BITFIELD_TYPE(Location, uint64_t)

ADD_BITFIELD_MEMBER(interesting, /*start:*/ 0,  /*len:*/ 1);    // 2 values
ADD_BITFIELD_MEMBER(file_id,     /*start:*/ 1,  /*len:*/ 29);   // 536,870,912 values
ADD_BITFIELD_MEMBER(line,        /*start:*/ 30, /*len:*/ 20);   // 1,048,576 values
ADD_BITFIELD_MEMBER(column,      /*start:*/ 50, /*len:*/ 14);   // 16,384 values

Location(bool interesting, FileId file_id, uint32_t line, uint32_t column) {
  this->interesting = interesting;
  this->file_id = file_id;
  this->line = line;
  this->column = column;
}

std::string ToString() {
  // Output looks like this:
  //
  //  *1:2:3
  //
  // * => interesting
  // 1 => file id
  // 2 => line
  // 3 => column

  std::string result;
  if (interesting)
    result += '*';
  result += std::to_string(file_id);
  result += ':';
  result += std::to_string(line);
  result += ':';
  result += std::to_string(column);
  return result;
}

// Compare two Locations and check if they are equal. Ignores the value of
// |interesting|.
// operator== doesn't seem to work properly...
bool IsEqualTo(const Location& o) {
  // When comparing, ignore the value of |interesting|.
  return (wrapper.value >> 1) == (o.wrapper.value >> 1);
}

Location WithInteresting(bool interesting) {
  Location result = *this;
  result.interesting = interesting;
  return result;
}

END_BITFIELD_TYPE()

struct IndexedFileDb {
  std::unordered_map<std::string, FileId> file_path_to_file_id;
  std::unordered_map<FileId, std::string> file_id_to_file_path;

  IndexedFileDb() {
    // Reserve id 0 for unfound.
    file_path_to_file_id[""] = 0;
    file_id_to_file_path[0] = "";
  }

  Location Resolve(const CXSourceLocation& cx_loc, bool interesting) {
    CXFile file;
    unsigned int line, column, offset;
    clang_getSpellingLocation(cx_loc, &file, &line, &column, &offset);

    FileId file_id;
    if (file != nullptr) {
      std::string path = clang::ToString(clang_getFileName(file));

      auto it = file_path_to_file_id.find(path);
      if (it != file_path_to_file_id.end()) {
        file_id = it->second;
      }
      else {
        file_id = file_path_to_file_id.size();
        file_path_to_file_id[path] = file_id;
        file_id_to_file_path[file_id] = path;
      }
    }

    return Location(interesting, file_id, line, column);
  }

  Location Resolve(const CXIdxLoc& cx_idx_loc, bool interesting) {
    CXSourceLocation cx_loc = clang_indexLoc_getCXSourceLocation(cx_idx_loc);
    return Resolve(cx_loc, interesting);
  }

  Location Resolve(const CXCursor& cx_cursor, bool interesting) {
    return Resolve(clang_getCursorLocation(cx_cursor), interesting);
  }

  Location Resolve(const clang::Cursor& cursor, bool interesting) {
    return Resolve(cursor.cx_cursor, interesting);
  }
};


template<typename T>
struct LocalId {
  uint64_t local_id;

  LocalId() : local_id(0) {} // Needed for containers. Do not use directly.
  explicit LocalId(uint64_t local_id) : local_id(local_id) {}

  bool operator==(const LocalId<T>& other) {
    return local_id == other.local_id;
  }
};

template<typename T>
bool operator==(const LocalId<T>& a, const LocalId<T>& b) {
  return a.local_id == b.local_id;
}

using TypeId = LocalId<IndexedTypeDef>;
using FuncId = LocalId<IndexedFuncDef>;
using VarId = LocalId<IndexedVarDef>;


template<typename T>
struct Ref {
  LocalId<T> id;
  Location loc;

  Ref(LocalId<T> id, Location loc) : id(id), loc(loc) {}
};
using TypeRef = Ref<IndexedTypeDef>;
using FuncRef = Ref<IndexedFuncDef>;
using VarRef = Ref<IndexedVarDef>;


// TODO: skip as much forward-processing as possible when |is_system_def| is
//       set to false.
// TODO: Either eliminate the defs created as a by-product of cross-referencing,
//       or do not emit things we don't have definitions for.

struct TypeDefDefinitionData {
  // General metadata.
  TypeId id;
  std::string usr;
  std::string short_name;
  std::string qualified_name;

  // While a class/type can technically have a separate declaration/definition,
  // it doesn't really happen in practice. The declaration never contains
  // comments or insightful information. The user always wants to jump from
  // the declaration to the definition - never the other way around like in
  // functions and (less often) variables.
  //
  // It's also difficult to identify a `class Foo;` statement with the clang
  // indexer API (it's doable using cursor AST traversal), so we don't bother
  // supporting the feature.
  optional<Location> definition;

  // If set, then this is the same underlying type as the given value (ie, this
  // type comes from a using or typedef statement).
  optional<TypeId> alias_of;

  // Immediate parent types.
  std::vector<TypeId> parents;

  // Types, functions, and variables defined in this type.
  std::vector<TypeId> types;
  std::vector<FuncId> funcs;
  std::vector<VarId> vars;

  TypeDefDefinitionData(TypeId id, const std::string& usr) : id(id), usr(usr) {}
};

struct IndexedTypeDef {
  TypeDefDefinitionData def;

  // Immediate derived types.
  std::vector<TypeId> derived;

  // Every usage, useful for things like renames.
  // NOTE: Do not insert directly! Use AddUsage instead.
  std::vector<Location> uses;

  bool is_system_def = false;

  IndexedTypeDef(TypeId id, const std::string& usr);
  void AddUsage(Location loc, bool insert_if_not_present = true);
};

struct FuncDefDefinitionData {
  // General metadata.
  FuncId id;
  std::string usr;
  std::string short_name;
  std::string qualified_name;
  optional<Location> definition;

  // Type which declares this one (ie, it is a method)
  optional<TypeId> declaring_type;

  // Method this method overrides.
  optional<FuncId> base;

  // Local variables defined in this function.
  std::vector<VarId> locals;

  // Functions that this function calls.
  std::vector<FuncRef> callees;

  FuncDefDefinitionData(FuncId id, const std::string& usr) : id(id), usr(usr) {
    assert(usr.size() > 0);
  }
};

struct IndexedFuncDef {
  FuncDefDefinitionData def;

  // Places the function is forward-declared.
  std::vector<Location> declarations;

  // Methods which directly override this one.
  std::vector<FuncId> derived;

  // Functions which call this one.
  // TODO: Functions can get called outside of just functions - for example,
  //       they can get called in static context (maybe redirect to main?)
  //       or in class initializer list (redirect to class ctor?)
  //    - Right now those usages will not get listed here (but they should be
  //      inside of all_uses).
  std::vector<FuncRef> callers;

  // All usages. For interesting usages, see callees.
  std::vector<Location> uses;

  bool is_system_def = false;

  IndexedFuncDef(FuncId id, const std::string& usr) : def(id, usr) {
    assert(usr.size() > 0);
  }
};

struct VarDefDefinitionData {
  // General metadata.
  VarId id;
  std::string usr;
  std::string short_name;
  std::string qualified_name;
  optional<Location> declaration;
  // TODO: definitions should be a list of locations, since there can be more
  //       than one.
  optional<Location> definition;

  // Type of the variable.
  optional<TypeId> variable_type;

  // Type which declares this one (ie, it is a method)
  optional<TypeId> declaring_type;

  VarDefDefinitionData(VarId id, const std::string& usr) : id(id), usr(usr) {}
};

struct IndexedVarDef {
  VarDefDefinitionData def;

  // Usages.
  std::vector<Location> uses;
  
  bool is_system_def = false;

  IndexedVarDef(VarId id, const std::string& usr) : def(id, usr) {
    assert(usr.size() > 0);
  }
};


struct IndexedFile {
  // NOTE: Every Id is resolved to a file_id of 0. The correct file_id needs
  //       to get fixed up when inserting into the real db.
  std::unordered_map<std::string, TypeId> usr_to_type_id;
  std::unordered_map<std::string, FuncId> usr_to_func_id;
  std::unordered_map<std::string, VarId> usr_to_var_id;

  std::vector<IndexedTypeDef> types;
  std::vector<IndexedFuncDef> funcs;
  std::vector<IndexedVarDef> vars;

  IndexedFileDb file_db;

  IndexedFile();

  TypeId ToTypeId(const std::string& usr);
  FuncId ToFuncId(const std::string& usr);
  VarId ToVarId(const std::string& usr);
  TypeId ToTypeId(const CXCursor& usr);
  FuncId ToFuncId(const CXCursor& usr);
  VarId ToVarId(const CXCursor& usr);

  IndexedTypeDef* Resolve(TypeId id);
  IndexedFuncDef* Resolve(FuncId id);
  IndexedVarDef* Resolve(VarId id);

  std::string ToString();
};



// TODO: Maybe instead of clearing/adding diffs, we should just clear out the
//       entire previous index and readd the new one? That would be simpler.
// TODO: ^^^ I don't think we can do this. It will probably stall the main
//       indexer for far too long since we will have to iterate over tons of
//       data.
// TODO: Idea: when indexing and joining to the main db, allow many dbs that
//             are joined to. So that way even if the main db is busy we can
//             still be joining. Joining the partially joined db to the main
//             db should be faster since we will have larger data lanes to use.
struct IndexedTypeDefDiff {};
struct IndexedFuncDefDiff {};
struct IndexedVarDefDiff {};

struct IndexedFileDiff {
  std::vector<IndexedTypeDefDiff> removed_types;
  std::vector<IndexedFuncDefDiff> removed_funcs;
  std::vector<IndexedVarDefDiff> removed_vars;

  std::vector<IndexedTypeDefDiff> added_types;
  std::vector<IndexedFuncDefDiff> added_funcs;
  std::vector<IndexedVarDefDiff> added_vars;

  // TODO: Instead of change, maybe we just remove and then add again? not sure.
  std::vector<IndexedTypeDefDiff> changed_types;
  std::vector<IndexedFuncDefDiff> changed_funcs;
  std::vector<IndexedVarDefDiff> changed_vars;
};

IndexedFile Parse(std::string filename, std::vector<std::string> args);