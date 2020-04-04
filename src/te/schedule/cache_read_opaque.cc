#include <tvm/te/schedule.h>
#include <tvm/te/operation.h>
#include <tvm/tir/stmt_functor.h>
#include <tvm/tir/ir_pass.h>
#include <unordered_set>
#include "message_passing.h"
#include "../../tir/pass/ir_util.h"
#include "../../arith/compute_expr.h"

namespace tvm {
namespace te {
  class AccessPattern {
  public:
    Map<Dimension, PrimExpr> args;
    const CallNode* original_access;
    const ComputeOpNode* reader_op;

    class Hasher {
    public:
      size_t operator()(const AccessPattern &pattern) const {
	return 0;
      }
    };
    class Equality {
    public:
      bool operator()(const AccessPattern & p1, const AccessPattern & p2) const {
	return (p1.args == p2.args);
      }
    };
  };

  using PatternsSet = std::unordered_set<AccessPattern, AccessPattern::Hasher, AccessPattern::Equality>;

  class AccessPatternCollector {
    class ExprAccessPatternCollector : public ExprVisitor {
      PrimExpr ExpandToLoopVars(PrimExpr expr, const ComputeOpNode* op) {
	class Expander: public ExprMutator {
	  PrimExpr VisitExpr_(const VarNode* op) override {
	    for (size_t i = 0; i < reader_op->index_variables.size(); ++i) {
	      if (op == reader_op->index_variables[i]->var.get()) {
		Array<PrimExpr> loop_vars;
		for (auto iv: reader_op->axis) {
		  loop_vars.push_back(iv->var);
		}
		return reader_op->index_expressions[i]->
		  substitute(loop_vars, reader_op->loop_dimensions);
	      }
	    }
	    return ExprMutator::VisitExpr_(op);
	  }

	public:
	  Expander(const ComputeOpNode* reader_op_) : reader_op(reader_op_) {}
	  const ComputeOpNode* reader_op;
	};
	return Expander(reader_op)(expr);
      }

      void VisitExpr_(const CallNode* op) override {
	if (op->func.defined()) {
	  Tensor t = Downcast<Operation>(op->func).output(op->value_index);
	  if (t->op.defined() && t == this->tensor) {
	    std::cout << "[APC] Found access " << GetRef<PrimExpr>(op) << std::endl;
	    AccessPattern ap;
	    for (size_t i = 0; i < op->args.size(); ++i) {
	      auto expanded = this->ExpandToLoopVars(op->args[i], reader_op);
	      ap.args.Set(tensor_index_dims[i], expanded);
	      std::cout << "[APC]   Expanded " << expanded << std::endl;
	    }
	    ap.original_access = op;
	    ap.reader_op = reader_op;
	    this->access_patterns->insert(ap);
	  }
	}
      }

    public:
      ExprAccessPatternCollector(const Tensor& tensor_, PatternsSet* access_patterns_, const ComputeOpNode* reader_op_) :
	tensor(tensor_), access_patterns(access_patterns_), reader_op(reader_op_) {
	if (auto op = tensor->op.as<ComputeOpNode>()) {
	  this->tensor_index_dims = op->self_index_dimensions;
	}
	else if (auto op = tensor->op.as<PlaceholderOpNode>()) {
	  this->tensor_index_dims = op->index_dimensions;
	}
	else {
	  CHECK(false) << "Cannot only cache Compute and Plcceholder operations";
	}
      }

      const Tensor& tensor;
      PatternsSet* access_patterns;
      const ComputeOpNode* reader_op;
      Array<Dimension> tensor_index_dims;
   };

    void collectAccesPatterns() {
      for (auto reader: this->readers) {
	if (auto reader_op = reader.as<ComputeOpNode>()) {
	  for (auto body_expr: reader_op->body) {
	    ExprAccessPatternCollector exprCollector(this->tensor, &(this->access_patterns), reader_op);
	    exprCollector(body_expr);
	  }
	}
	else {
	  CHECK(false) <<
	    "Opaque caching is not yet implemented for reader op " << reader;
	}
      }
    }

  public:
    PatternsSet collect() {
      collectAccesPatterns();
      return this->access_patterns;
    }

    AccessPatternCollector(const Tensor& tensor_, const Array<Operation>& readers_) :
      tensor(tensor_), readers(readers_) {}

    PatternsSet access_patterns;
    const Tensor& tensor;
    const Array<Operation>& readers;
  };

  class TranslateVarsCrossStages: public ExprMutator {
    PrimExpr VisitExpr_(const VarNode* op) override {

      Dimension var_dim;
      for (size_t i = 0; i < reader_op->index_variables.size(); ++i) {
	if (op == reader_op->index_variables[i]->var.get()) {
	  var_dim = reader_op->index_dimensions[i];
	  std::cout << "Found dim " << var_dim->name << std::endl;
	}
      }

      for (size_t i = 0; i < index_dimensions.size(); ++i) {
	if (var_dim == index_dimensions[i]) {
	  std::cout << "Found dim var " << index_variables[i] << std::endl;
	  return index_variables[i]->var;
	}
      }

      for (size_t i = 0; i < loop_dimensions.size(); ++i) {
	if (var_dim == loop_dimensions[i]) {
	  std::cout << "Found dim var " << loop_variables[i] << std::endl;
	  return loop_variables[i]->var;
	}
      }
      return ExprMutator::VisitExpr_(op);
    }

  public:
    TranslateVarsCrossStages(const CallNode* op_, const ComputeOpNode* reader_op_, Array<IterVar>& index_variables_,
			     Array<IterVar>& loop_variables_, Array<Dimension>& index_dimensions_,
			     Array<Dimension>& loop_dimensions_) :
      op(op_), reader_op(reader_op_), index_variables(index_variables_),
      loop_variables(loop_variables_), index_dimensions(index_dimensions_),
      loop_dimensions(loop_dimensions_){}

    const CallNode* op;
    const ComputeOpNode* reader_op;
    Array<IterVar> index_variables;
    Array<IterVar> loop_variables;
    Array<Dimension> index_dimensions;
    Array<Dimension> loop_dimensions;
  };

  PrimExpr CacheBodyBuilder(std::vector<AccessPattern> patterns_vec, Array<IterVar> index_variables, Array<IterVar> loop_variables,
			    Array<Dimension> index_dimensions, Array<Dimension> loop_dimensions) {
    const Var variant_loop_var = loop_variables[loop_variables.size() - 1]->var;

    PrimExpr body = PrimExpr(0);
    for (size_t i = 0; i < patterns_vec.size(); ++i) {
      AccessPattern pattern = patterns_vec[i];
      body = if_then_else(variant_loop_var == static_cast<int>(i),
			  TranslateVarsCrossStages(pattern.original_access, pattern.reader_op, index_variables,
						   loop_variables, index_dimensions, loop_dimensions)
			  (GetRef<PrimExpr>(pattern.original_access)),
			  body);
    }

    std::cout << "Body " << body << std::endl;
    return body;
  }

  Tensor Schedule::cache_read_opaque(const Tensor& tensor,
				     const std::string& scope,
				     const Array<Operation>& readers) {
    const ComputeOpNode* compute_op = tensor->op.as<ComputeOpNode>();
    const PlaceholderOpNode* placeholder_op = tensor->op.as<PlaceholderOpNode>();
    AccessPatternCollector collector(tensor, readers);
    PatternsSet patterns = collector.collect();
    std::cout << "Patterns: " << patterns.size() << std::endl;

    /************* Create the cache stage *************/
    Array<IterVar> original_loop_axis;
    Array<Dimension> original_loop_dimensions;
    if (compute_op) {
      original_loop_axis = compute_op->axis;
      original_loop_dimensions = compute_op->loop_dimensions;
    }
    else {
      original_loop_axis = placeholder_op->axis;
      original_loop_dimensions = placeholder_op->loop_dimensions;
    }

    // Create the body of the cache stage
    std::string cache_name = tensor->op->name + "." + scope;
    std::string cache_tag = {};
    Map<std::string, ObjectRef> cache_attrs = {};

    Array<IterVar> cache_axis;
    {
      for (size_t i = 0; i < original_loop_axis.size(); ++i) {
	auto lv = original_loop_axis[i];
	cache_axis.push_back(IterVarNode::make(lv->dom, Var("lv" + std::to_string(i), DataType::Int(32)), lv->iter_type,
					       lv->loop_axis, lv->thread_tag));
      }
      cache_axis.push_back(IterVarNode::make(Range(0, static_cast<int>(patterns.size())), Var("var", DataType::Int(32)),
					     IterVarType::kDataPar, ""));
    }

    Array<PrimExpr> cache_shape;
    {
      for (size_t i = 0; i < original_loop_axis.size(); ++i) {
	// TODO(ppf):: Take actual loop dim extents
	cache_shape.push_back(1000);
      }
      // Pattern dimension
      cache_shape.push_back(static_cast<int>(patterns.size()));
    }

    Array<IterVar> cache_index_variables;
    Array<UninterpFun> cache_index_expressions;
    Array<Dimension> cache_index_dimensions;
    {
      if (compute_op) {
	for (size_t i = 0; i < compute_op->index_variables.size(); ++i) {
	  auto iv = compute_op->index_variables[i];
	  cache_index_variables.push_back(IterVarNode::make(iv->dom, Var("iv" + std::to_string(i), DataType::Int(32)),
							    iv->iter_type, iv->thread_tag));
	}

	cache_index_expressions = Array<UninterpFun>(compute_op->index_expressions);
	cache_index_dimensions = Array<Dimension>(compute_op->index_dimensions);
      }
      else {
	for (size_t i = 0; i < placeholder_op->index_expressions.size(); ++i) {
	  auto uif = placeholder_op->index_expressions[i];
	  cache_index_variables.push_back(IterVarNode::make(uif->range, Var("iv" + std::to_string(i), DataType::Int(32)),
							    IterVarType::kDataPar, ""));
	}

	cache_index_expressions = Array<UninterpFun>(placeholder_op->index_expressions);
	cache_index_dimensions = Array<Dimension>(placeholder_op->index_dimensions);
      }
    }

    Array<Dimension> cache_loop_dimensions;
    Array<Dimension> cache_self_index_dimensions;
    {
      cache_loop_dimensions = Array<Dimension>(original_loop_dimensions);
      cache_self_index_dimensions = Array<Dimension>(original_loop_dimensions);
      auto variant_dim = DimensionNode::make("variants", DimensionNode::DimensionType::kRangeDim);
      cache_loop_dimensions.push_back(variant_dim);
      cache_self_index_dimensions.push_back(variant_dim);
    }

    std::vector<AccessPattern> patterns_vec;
    for (auto pattern: patterns) {
      patterns_vec.push_back(pattern);
    }

    Array<PrimExpr> cache_body = { CacheBodyBuilder(patterns_vec, cache_index_variables, cache_axis,
						    cache_index_dimensions, cache_loop_dimensions) };

    Tensor cache = ComputeOpNode::make(cache_name, cache_tag, cache_attrs, cache_axis, cache_shape,
				       cache_index_variables, cache_index_expressions, cache_loop_dimensions,
				       cache_index_dimensions, cache_self_index_dimensions, cache_body).output(0);
    return cache;
  }
}
}
