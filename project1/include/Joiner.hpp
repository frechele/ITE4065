#pragma once
#include <cstdint>
#include <set>
#include <vector>
#include "Operators.hpp"
#include "Parser.hpp"
#include "Relation.hpp"
//---------------------------------------------------------------------------
class Joiner
{
    /// Add scan to query
    std::unique_ptr<Operator> addScan(std::set<unsigned>& usedRelations,
                                      SelectInfo& info, QueryInfo& query);

 public:
    /// The relations that might be joined
    std::vector<Relation> relations;
    /// Add relation
    void addRelation(const char* fileName);
    /// Get relation
    Relation& getRelation(unsigned id);
    /// Joins a given set of relations
    std::string join(QueryInfo& i);
};
//---------------------------------------------------------------------------
