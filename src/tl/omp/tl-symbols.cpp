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
#include "tl-omptransform.hpp"

namespace TL
{
    bool OpenMPTransform::is_nonstatic_member_function(FunctionDefinition function_definition)
    {
        IdExpression function_name = function_definition.get_function_name();
        Symbol function_symbol = function_name.get_symbol();

        // It must be a member
        if (!function_symbol.is_member())
        {
            return false;
        }

        Statement function_body = function_definition.get_function_body();
        Scope function_body_scope = function_body.get_scope();

        Symbol sym = function_body_scope.get_symbol_from_name("this");

        if (!sym.is_valid())
        {
            return false;
        }

        return true;
    }

    bool OpenMPTransform::is_unqualified_member_symbol(IdExpression id_expression, FunctionDefinition function_definition)
    {
        Symbol current_symbol = id_expression.get_symbol();
        Symbol function_symbol = function_definition.get_function_name().get_symbol();

        if (function_symbol.is_member()
                && current_symbol.is_member()
                && (function_symbol.get_class_type() 
                    == current_symbol.get_class_type()))
        {
            return 1;
        }

        return 0;
    }

    bool OpenMPTransform::is_function_accessible(IdExpression id_expression, 
            FunctionDefinition function_definition)
    {
        Symbol current_symbol = id_expression.get_symbol();

        Scope function_scope = function_definition.get_scope();
        Symbol function_visible_symbol = function_scope.get_symbol_from_id_expr(id_expression.get_ast());

        return (function_visible_symbol.is_valid()
                && function_visible_symbol == current_symbol);
    }
}
