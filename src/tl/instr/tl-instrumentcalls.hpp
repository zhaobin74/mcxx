#ifndef TL_INSTRUMENTCALLS_HPP
#define TL_INSTRUMENTCALLS_HPP

#include "tl-compilerphase.hpp"
#include "tl-traverse.hpp"
#include "tl-instrumentfilter.hpp"
#include "tl-langconstruct.hpp"
#include "tl-externalvars.hpp"

namespace TL
{
    class InstrumentCalls : public CompilerPhase
    {
        private:
            InstrumentFilterFile _instrument_filter;
            class InstrumentCallsFunctor : public TraverseFunctor
            {
                private:
                    std::set<std::string> defined_shadows;
                    InstrumentFilterFile& _instrument_filter;
                public:
                    InstrumentCallsFunctor(InstrumentFilterFile& instrument_filter);

                    virtual ~InstrumentCallsFunctor() { }

                    virtual void preorder(Context ctx, AST_t node);
                    virtual void postorder(Context ctx, AST_t node);

                    bool define_shadow(IdExpression function_name, std::string shadow_function_name);
            };

            class MainWrapper : public TraverseFunctor
            {
                private:
                    ScopeLink _sl;
                public:
                    MainWrapper(ScopeLink sl);

                    virtual void preorder(Context ctx, AST_t node);
                    virtual void postorder(Context ctx, AST_t node);

                    virtual ~MainWrapper() { }
            };

            class MainPredicate : public Predicate<AST_t>
            {
                private:
                    ScopeLink _sl;
                    PredicateBool<LANG_IS_FUNCTION_DEFINITION> is_function_def;
                public:
                    MainPredicate(ScopeLink& sl);

                    virtual bool operator()(AST_t& t) const;
                    virtual ~MainPredicate() { }
            };

        public:
            virtual void run(DTO& data_flow);

            virtual ~InstrumentCalls();
            InstrumentCalls();
    };
}

#endif // TL_INSTRUMENTCALLS_HPP
