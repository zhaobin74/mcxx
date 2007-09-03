/*
    Mercurium C/C++ Compiler
    Copyright (C) 2006-2007 - Roger Ferrer Ibanez <roger.ferrer@bsc.es>
    Barcelona Supercomputing Center - Centro Nacional de Supercomputacion
    Universitat Politecnica de Catalunya

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifndef CXX_TYPEUTILS_H
#define CXX_TYPEUTILS_H

#include "cxx-ast.h"
#include "cxx-scope.h"
#include "cxx-buildscope.h"
#include "cxx-macros.h"

MCXX_BEGIN_DECLS

enum cv_equivalence_t
{
    CVE_UNKNOWN = 0,
    CVE_IGNORE_OUTERMOST,
    CVE_CONSIDER
};

char equivalent_types(type_t* t1, type_t* t2,
        enum cv_equivalence_t cv_equiv, decl_context_t decl_context);
char overloaded_function(type_t* f1, type_t* f2,
        decl_context_t decl_context);

/* Copy functions */
class_info_t* copy_class_info(class_info_t* class_info);
simple_type_t* copy_simple_type(simple_type_t* type_info);
type_t* copy_type(type_t* type);
function_info_t* copy_function_info(function_info_t* function_info);
array_info_t* copy_array_info(array_info_t* array_info);
pointer_info_t* copy_pointer_info(pointer_info_t* pointer_info);
enum_info_t* copy_enum_info(enum_info_t* enum_info);
template_argument_list_t* copy_template_argument_list(template_argument_list_t* template_argument_list);

// Equality functions
char equivalent_builtin_type(simple_type_t *t1, simple_type_t *t2);

// Conversion functions
type_t* simple_type_to_type(simple_type_t* simple_type_info);
char equivalent_simple_types(simple_type_t *t1, simple_type_t *t2,
        decl_context_t decl_context);

cv_qualifier_t* get_outermost_cv_qualifier(type_t* t);

// Query functions
char* get_builtin_type_name(simple_type_t* simple_type_info, decl_context_t decl_context);
type_t* base_type(type_t* t);

type_t* advance_over_typedefs(type_t* t);
type_t* advance_over_typedefs_with_cv_qualif(type_t* t1, cv_qualifier_t* cv_qualif);

// Debug purpose functions
char* print_declarator(type_t* printed_declarator, decl_context_t decl_context);

// Query functions
char is_fundamental_type(type_t* t);

char is_integral_type(type_t* t);
char is_unsigned_long_int(type_t* t);
char is_unsigned_long_long_int(type_t* t);
char is_long_long_int(type_t* t);
char is_long_int(type_t* t);
char is_unsigned_int(type_t* t);
char is_enumerated_type(type_t* t);

char is_floating_type(type_t* t);
char is_long_double(type_t* t);
char is_float(type_t* t);

char is_function_type(type_t* t);
type_t* function_return_type(type_t* t);
type_t** function_parameter_types(type_t* t, int* num_params, char* has_ellipsis);

char can_be_promoted_to_dest(type_t* orig, type_t* dest);
char can_be_converted_to_dest(type_t* orig, type_t* dest);

char is_reference_type(type_t* t1);
type_t* reference_referenced_type(type_t* t1);
char is_reference_related(type_t* rt1, type_t* rt2, 
        decl_context_t decl_context);
char is_reference_compatible(type_t* t1, type_t* t2, 
        decl_context_t decl_context);

char pointer_can_be_converted_to_dest(type_t* orig, type_t* dest, 
        char* to_void, char* derived_to_base, char* cv_adjust,
        decl_context_t decl_context);

char is_class_type(type_t* possible_class);
char is_unnamed_class_type(type_t* possible_class);
char is_named_class_type(type_t* possible_class);
char is_base_class_of(type_t* possible_base, type_t* possible_derived);
type_t* get_class_type(type_t* class_type);
scope_entry_t* get_class_symbol(type_t* class_type);

char is_named_type(type_t* t);
scope_entry_t* get_symbol_of_named_type(type_t* t);

char is_dependent_type(type_t* type, decl_context_t decl_context);
char is_dependent_expression(AST expr, decl_context_t decl_context);

char is_specialized_class_type(type_t* type);

cv_qualifier_t get_cv_qualifier(type_t* type_info);

char is_direct_type(type_t* t1);
char is_bool_type(type_t* t1);
char is_pointer_type(type_t* t1);
char is_direct_type(type_t* t);
type_t* pointer_pointee_type(type_t* t);
char is_pointer_to_member_type(type_t* t);
char is_array_type(type_t* t1);
type_t* array_element_type(type_t* t1);

char is_void_pointer_type(type_t* t1);
char is_pointer_to_class_type(type_t* t1);
char is_reference_to_class_type(type_t* t1);
char is_typedef_type(type_t* t1);

char equivalent_cv_qualification(cv_qualifier_t cv1, cv_qualifier_t cv2);

#if 0
char is_dependent_tree(AST tree, scope_t* st) __attribute__((deprecated));
#endif

scope_entry_t* give_real_entry(scope_entry_t* entry);

cv_qualifier_t* get_innermost_cv_qualifier(type_t* t);

char* get_declaration_string_internal(type_t* type_info, 
        decl_context_t decl_context,
        const char* symbol_name, 
        const char* initializer, 
        char semicolon,
        int *num_parameter_names,
        char ***parameter_names,
        char is_parameter);

char* get_simple_type_name_string(decl_context_t decl_context, type_t* type_info);

MCXX_END_DECLS

#endif // CXX_TYPEUTILS_H
