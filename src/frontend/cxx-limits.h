#ifndef CXX_LIMITS_H
#define CXX_LIMITS_H

enum 
{
    // AST limits
    MCXX_MAX_AST_CHILDREN = 4,

    // Function limits
    MCXX_MAX_FUNCTION_PARAMETERS = 256,
    MCXX_MAX_FUNCTIONS_PER_CLASS = 1024,
    MCXX_MAX_FUNCTION_CALL_ARGUMENTS = 256,

    // Class limits
    MCXX_MAX_CLASS_BASES = 256,

    // C++ overload
    MCXX_MAX_BUILTINS_IN_OVERLOAD = 256,
    MCXX_MAX_SURROGATE_FUNCTIONS = 64,
    MCXX_MAX_USER_DEFINED_CONVERSIONS = 64,

    // Template limits
    MCXX_MAX_TEMPLATE_PARAMETERS = 256,
    MCXX_MAX_TEMPLATE_ARGUMENTS = 256,
    MCXX_MAX_FEASIBLE_SPECIALIZATIONS = 256,
    MCXX_MAX_ARGUMENTS_FOR_DEDUCTION = 256,

    // Scope limits
    MCXX_MAX_SCOPES_NESTING = 128,

    // C99 Designator
    MCXX_MAX_DESIGNATORS = 64,

    // GCC attributes
    MCXX_MAX_GCC_ATTRIBUTES_PER_SYMBOL = 256,

    // C++ associated scopes during ADL
    MCXX_MAX_KOENIG_ASSOCIATED_SCOPES = 256,

    // C++ associated namespaces during lookup
    MCXX_MAX_ASSOCIATED_NAMESPACES = 256,

    // Environmental limits
    MCXX_MAX_BYTES_INTEGER = 16,

    // Type limits
    MCXX_MAX_QUALIFIER_CONVERSION = 256,

    // Fortran modules limits
    MCXX_MAX_RENAMED_SYMBOLS = 256,
    MCXX_MAX_ARRAY_SPECIFIER = 16,
};

#endif // CXX_LIMITS_H
