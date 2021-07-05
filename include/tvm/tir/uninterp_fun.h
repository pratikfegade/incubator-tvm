#ifndef TVM_TIR_UNINTERP_FUN_H_
#define TVM_TIR_UNINTERP_FUN_H_

#include <tvm/arith/int_set.h>
#include <tvm/ir/expr.h>
#include <tvm/runtime/container.h>
#include <tvm/te/dimension.h>
#include <tvm/tir/expr.h>
#include <tvm/tir/stmt.h>

#include <vector>

namespace tvm {
namespace te {
/*! \brief container class of iteration variable. */
class Dimension;
}  // namespace te

namespace tir {
/*! \brief container class of iteration variable. */

class UninterpFun;

struct ArgMappingAndEquality {
  bool equals;
  Map<Var, Var> mapping;
};

/*!
 * \brief Uninterpreted function node
 */
class UninterpFunNode : public FunctionBaseNode {
 public:
  /*!
   * \brief the name of the function
   */
  std::string fname;
  /*! \brief the parameters */
  Array<Var> parameters;
  /*! \brief named dimensions corresponding to the parameteres */
  Array<tvm::te::Dimension> dimensions;
  /*! \brief The body if the function */
  PrimExpr body;
  /*! \brief The range of the function */
  Range range;

  void VisitAttrs(AttrVisitor* v) {
    v->Visit("fname", &fname);
    v->Visit("paramters", &parameters);
    v->Visit("body", &body);
  }

  TVM_DLL static UninterpFun make(std::string fname, Range range,
                                  Array<tvm::te::Dimension> dimensions, Array<Var> parameters,
                                  PrimExpr body);

  TVM_DLL static UninterpFun from_constant(std::string fname, PrimExpr val);

  /*! \brief Get the name. */
  const std::string& func_name() const final { return fname; }

  int num_outputs() const;

  bool is_complex() const;

  void SetBody(PrimExpr expr);

  void SetRange(Range r);

  /*! \brief Get the arity. */
  size_t arity() const;

  int GetArgPos(Var var) const;

  const PrimExpr substitute(Array<PrimExpr> arguments, Array<tvm::te::Dimension> dimensions) const;

  static constexpr const char* _type_key = "tir.UninterpFun";
  TVM_DECLARE_FINAL_OBJECT_INFO(UninterpFunNode, Object);
};

/*!
 * \brief Uninterpreted function
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

  const PrimExpr MakeCallTo(Array<PrimExpr> args, Array<Dimension> arg_dims,
                            DataType dtype = DataType::Handle()) const {
    return UninterpFun::MakeCallTo(*this, args, arg_dims, dtype);
  }

  static PrimExpr InlineUninterpFunCalls(PrimExpr e, bool only_simple = false);

  static Stmt InlineUninterpFunCalls(Stmt e, bool only_simple = false);

  static Range InlineUninterpFunCalls(Range r, bool only_simple = false);

  static Map<Dimension, PrimExpr> InvertCall(PrimExpr call, UninterpFun ufun);

  static ArgMappingAndEquality CheckEquality(UninterpFun f1, UninterpFun f2);

  static PrimExpr MakeCallTo(UninterpFun f, Array<PrimExpr> args, Array<Dimension> arg_dims,
                             DataType dtype = DataType::Handle());

  static PrimExpr RelaxComplexUninterpCallsMaxInclusive(PrimExpr expr);
};

inline const UninterpFunNode* UninterpFun::operator->() const {
  return static_cast<const UninterpFunNode*>(data_.get());
}

}  // namespace tir
}  // namespace tvm

#endif
