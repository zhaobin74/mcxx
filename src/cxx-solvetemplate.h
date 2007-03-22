#ifndef CXX_SOLVETEMPLATE_H
#define CXX_SOLVETEMPLATE_H

#include "cxx-macros.h"

MCXX_BEGIN_DECLS

#include "cxx-scope.h"
#include "cxx-typeunif.h"

typedef 
struct matching_pair_tag
{
    scope_entry_t* entry;
    unification_set_t* unif_set;
} matching_pair_t;

#include "cxx-buildscope.h"

matching_pair_t* solve_template(scope_entry_list_t* candidate_templates, template_argument_list_t* arguments, scope_t* st,
        char give_exact_match, decl_context_t decl_context);
char match_one_template(template_argument_list_t* arguments, 
        template_argument_list_t* specialized, scope_entry_t* specialized_entry, 
        scope_t* st, unification_set_t* unif_set,
        decl_context_t decl_context);

MCXX_END_DECLS

#endif // CXX_SOLVETEMPLATE_H
