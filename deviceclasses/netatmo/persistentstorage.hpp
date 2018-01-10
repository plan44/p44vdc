/*
 *  Copyright (c) 2017 digitalSTROM.org, Zurich, Switzerland
 *
 *  This file is part of ds-vdc
 */
#pragma once

#if __cplusplus >= 201402L

#include <string>
#include "persistentparams.hpp"

namespace p44 {

namespace detail {


template <typename T>
using PairType = std::pair<const char*, T&>;


class Field
{
protected:
  p44::FieldDefinition fieldDef;

  Field(const p44::FieldDefinition& aFieldDef) : fieldDef(aFieldDef) {}

public:
  virtual ~Field() {}
  p44::FieldDefinition* getFieldDef() { return &fieldDef; }
  virtual void bindToStatement(sqlite3pp::statement& aStatement, int aIndex) = 0;
  virtual void loadFromRow(sqlite3pp::query::iterator& aRow, int aIndex) = 0;
};

class StringField : public Field
{
  using inherited = Field;
  std::string& data;

public:
  StringField(const char* aName, std::string& aData) : inherited({ aName, SQLITE_TEXT }), data(aData) {}

  void bindToStatement(sqlite3pp::statement& aStatement, int aIndex) P44_OVERRIDE
  {
    aStatement.bind(aIndex++, data.c_str(), false);
  }

  void loadFromRow(sqlite3pp::query::iterator& aRow, int aIndex) P44_OVERRIDE
  {
    data = nonNullCStr(aRow->get<const char *>(aIndex));
  }
};

class BoolField : public Field
{
  using inherited = Field;
  bool& data;

public:

  BoolField(const char* aName, bool& aData) : inherited({ aName, SQLITE_INTEGER }), data(aData) {}

  void bindToStatement(sqlite3pp::statement& aStatement, int aIndex) P44_OVERRIDE
  {
    aStatement.bind(aIndex, data);
  }

  void loadFromRow(sqlite3pp::query::iterator& aRow, int aIndex) P44_OVERRIDE
  {
    data = aRow->get<bool>(aIndex);
  }
};
}

template <typename ...Args>
class PersistentStorage : public PersistentParams
{
  std::string name;
  std::array<std::unique_ptr<detail::Field>, sizeof...(Args)> fieldDefs;

  using inherited = PersistentParams;

  size_t numAllFields() { return fieldDefs.size(); }


  void add(detail::PairType<std::string> aField, size_t aIndex)
  {
    fieldDefs[aIndex] = std::make_unique<detail::StringField>(aField.first, aField.second);
  }

  void add(detail::PairType<bool> aField, size_t aIndex)
  {
    fieldDefs[aIndex] = std::make_unique<detail::BoolField>(aField.first, aField.second);
  }

public:
  PersistentStorage(const std::string& aTableName, ParamStore &aParamStore, detail::PairType<Args>... aArgs) :
    inherited(aParamStore),
    name(aTableName)
  {
    size_t index = 0;
    using expand_type = int[];
    expand_type { (add(aArgs, index++), 0)... };
  }

  void load(const string& aRowId)
  {
    loadFromStore(aRowId.c_str());
  }

  void save(const string& aRowId)
  {
    markDirty();
    saveToStore(aRowId.c_str(), true);
  }

  const char *tableName() P44_OVERRIDE { return name.c_str(); };

  size_t numFieldDefs() P44_OVERRIDE
  {
    return inherited::numFieldDefs() + numAllFields();
  }

  const FieldDefinition* getFieldDef(size_t aIndex) P44_OVERRIDE
  {
    if (aIndex<inherited::numFieldDefs())
      return inherited::getFieldDef(aIndex);
    aIndex -= inherited::numFieldDefs();
    if (aIndex<numAllFields())
      return fieldDefs[aIndex]->getFieldDef();
    return nullptr;
  }


  void loadFromRow(sqlite3pp::query::iterator& aRow, int& aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE
  {
    inherited::loadFromRow(aRow, aIndex, aCommonFlagsP);

    for(size_t i = 0; i < numFieldDefs(); i++) {
      fieldDefs[i]->loadFromRow(aRow, aIndex++);
    }
  }

  void bindToStatement(sqlite3pp::statement& aStatement, int& aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE
  {
    inherited::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);

    for(size_t i = 0 ; i < numFieldDefs(); i++) {
      fieldDefs[i]->bindToStatement(aStatement, aIndex++);
    }
  }
};

template <typename ...Args>
class PersistentStorageWithRowId : public PersistentStorage<Args...>
{
  std::string rowId;

  using inherited = PersistentStorage<Args...>;

public:

  PersistentStorageWithRowId(const string& aRowId, const std::string& aTableName, ParamStore &aParamStore, detail::PairType<Args>&&... aArgs) :
    rowId(aRowId),
    inherited(aTableName, aParamStore, std::forward<detail::PairType<Args>>(aArgs)...) {}

  void load()
  {
    inherited::load(rowId);
  }

  void save()
  {
    inherited::save(rowId);
  }
};

} // namespace p44

#endif

