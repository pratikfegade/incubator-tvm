#ifndef TVM_TIR_UNINTERP_FUN_H_
#define TVM_TIR_UNINTERP_FUN_H_

#include <tvm/ir/expr.h>
#include <tvm/tir/expr.h>
#include <unordered_map>

namespace tvm {
  namespace tir {
    /*! \brief container class of iteration variable. */
    class UninterpFun;

    /*!
     * \brief Uinterpreted function node
     */
    class UninterpFunNode : public FunctionBaseNode {
    public:
      /*!
       * \brief the name of the function
       */
      std::string fname;
      /*! \brief the parameters */
      Array<Var> parameters;
      /*! \brief The body if the function */
      PrimExpr body;

      void VisitAttrs(AttrVisitor* v) {
	v->Visit("fname", &fname);
	v->Visit("paramters", &parameters);
	v->Visit("body", &body);
      }

      TVM_DLL static UninterpFun make(std::string fname,
				      Array<Var> parameters, PrimExpr body);

      /*! \brief Get the name. */
      const std::string& func_name() const final {
	return fname;
      }

      int num_outputs() const;

      bool is_complex() const;

      /*! \brief Get the arity. */
      size_t arity() const;

      /*! \brief Get the arity. */
      const PrimExpr substitute(Array<PrimExpr> arguments) const;

      static constexpr const char* _type_key = "tir.UninterpFun";
      TVM_DECLARE_FINAL_OBJECT_INFO(UninterpFunNode, Object);
    };

    /*!
     * \brief Uinterpreted function
     */
    class UninterpFun : public FunctionRef {
    public:
      UninterpFun() {}
      // construct from shared ptr.
      explicit UninterpFun(ObjectPtr<Object> n) : FunctionRef(n) {}
      /*!
       * \brief access the internal node container
       * \return the pointer to the internal node container
       */
      inline const UninterpFunNode* operator->() const;

      /*! \brief specify container node */
      using ContainerType = UninterpFunNode;
    };

    inline const UninterpFunNode* UninterpFun::operator->() const {
      return static_cast<const UninterpFunNode*>(data_.get());
    }
  }
}

#endif
