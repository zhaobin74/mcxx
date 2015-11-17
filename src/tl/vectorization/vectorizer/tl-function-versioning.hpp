/*--------------------------------------------------------------------
  (C) Copyright 2006-2014 Barcelona Supercomputing Center
                          Centro Nacional de Supercomputacion
  
  This file is part of Mercurium C/C++ source-to-source compiler.
  
  See AUTHORS file in the top level directory for information
  regarding developers and contributors.
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.
  
  Mercurium C/C++ source-to-source compiler is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU Lesser General Public License for more
  details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with Mercurium C/C++ source-to-source compiler; if
  not, write to the Free Software Foundation, Inc., 675 Mass Ave,
  Cambridge, MA 02139, USA.
--------------------------------------------------------------------*/

#ifndef TL_FUNCTION_VERSIONING_HPP
#define TL_FUNCTION_VERSIONING_HPP

#include "tl-nodecl-base.hpp"
#include <map>


namespace TL 
{ 
    namespace Vectorization 
    {
        enum FunctionPriority{ SIMD_FUNC_PRIORITY = 2, DEFAULT_FUNC_PRIORITY = 1, NAIVE_FUNC_PRIORITY = 0};

        class VectorFunctionVersion
        {
            private:
                const TL::Symbol _func_symbol;
                const FunctionPriority _priority;
                const std::string _device;
                const unsigned int _vector_length;
                const TL::Type _target_type;
                const bool _masked;
                const bool _is_svml;

            public:
                VectorFunctionVersion(const TL::Symbol& func_symbol, 
                        const std::string& device, 
                        const unsigned int vector_length, 
                        const TL::Type& _target_type,
                        const bool masked,
                        const FunctionPriority priority,
                        const bool is_svml);

                const TL::Symbol get_version() const;
                bool has_kind(const std::string& device,
                        const unsigned int vector_length,
                        const TL::Type& target_type,
                        const bool masked) const;
                bool is_better_than(const VectorFunctionVersion& func_version) const;
                bool is_svml_function() const;
        };


        class FunctionVersioning
        {
            typedef std::multimap<TL::Symbol, const VectorFunctionVersion> versions_map_t;
            versions_map_t _versions;

            private:
                const VectorFunctionVersion* get_best_function_version(TL::Symbol func_name, 
                        const std::string& device,
                        const unsigned int vector_length,
                        const TL::Type& _target_type,
                        const bool masked) const;

                versions_map_t::const_iterator find_best_function(TL::Symbol func_name,
                        const std::string& device,
                        const unsigned int vector_length,
                        const Type& target_type,
                        const bool masked) const;
 
            public:
                FunctionVersioning();

                void clear();
                void add_version(TL::Symbol func_name, const VectorFunctionVersion& func_version);
                const TL::Symbol get_best_version(TL::Symbol func_symbol, 
                        const std::string& device,
                        const unsigned int vector_length,
                        const TL::Type& _target_type,
                        const bool masked) const;

                bool is_svml_function(TL::Symbol func_name, 
                        const std::string& device,
                        const unsigned int vector_length,
                        const TL::Type& _target_type,
                        const bool masked) const;
        };
    }
}

#endif //TL_FUNCTION_VERSIONING_HPP

