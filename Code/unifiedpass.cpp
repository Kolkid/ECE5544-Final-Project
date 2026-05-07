// ECE/CS 5544 Assignment 3 unifiedpass.cpp
// Lean starter: buildable scaffolds, minimal solved.
// Code modified by: Andrew Merdes, Jacqueline Newland

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <memory>

using namespace llvm;

namespace {
    std::string getShortValueName(const Value* V) {
        if (!V) return "(null)";
        if (V->hasName()) return "%" + V->getName().str();
        if (const auto* C = dyn_cast<ConstantInt>(V)) return std::to_string(C->getSExtValue());
        std::string S;
        raw_string_ostream OS(S);
        V->printAsOperand(OS, false);
        return S;
    }

    // -------------------- Available Expressions (starter) --------------------
    //Expression structure
    struct Expr {
        Instruction::BinaryOps opcode;
        Value* lhs;
        Value* rhs;
        auto operator<=>(const Expr&) const = default;

        static Expr fromBO(const BinaryOperator& BO) {
            return { BO.getOpcode(), BO.getOperand(0), BO.getOperand(1) };
        }
    };
    //print an expression
    raw_ostream& operator<<(raw_ostream& OS, const Expr& E) {
        OS << getShortValueName(E.lhs) << " ";
        switch (E.opcode) {
        case Instruction::Add: OS << "+"; break;
        case Instruction::Sub: OS << "-"; break;
        case Instruction::Mul: OS << "*"; break;
        case Instruction::SDiv:
        case Instruction::UDiv: OS << "/"; break;
        default: OS << "(op)"; break;
        }
        OS << " " << getShortValueName(E.rhs);
        return OS;
    }
    //print a bit vector
    template <typename T>
    void printBitSet(raw_ostream& OS, StringRef label, const BitVector& bits, const std::vector<T>& universe) {
        OS << "  " << label << ": { ";
        bool first = true;
        for (unsigned i = 0; i < bits.size(); ++i) {
            if (!bits.test(i)) continue;
            if (!first) OS << "; ";
            first = false;
            OS << universe[i];
        }
        OS << " }\n";
    }

    struct AvailablePass : PassInfoMixin<AvailablePass> {
        struct BlockState {
            BitVector in;
            BitVector out;
            BitVector gen;
            BitVector kill;
        };
        //intersection between bit vectors
        static BitVector meetIntersect(const std::vector<BitVector>& ins) {
            if (ins.empty()) return {};
            BitVector out = ins[0];
            for (size_t i = 1; i < ins.size(); ++i) out &= ins[i];
            return out;
        }

        PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
            //print function name
            outs() << "=== ";
            F.printAsOperand(outs(), false);
            outs() << " ===\n";
            //determine all expressions in the function
            std::vector<Expr> universe;
            for (auto& BB : F) {
                for (auto& I : BB) {
                    if (auto* BO = dyn_cast<BinaryOperator>(&I)) universe.push_back(Expr::fromBO(*BO));
                }
            }
            //order and remove duplicates
            std::sort(universe.begin(), universe.end());
            universe.erase(std::unique(universe.begin(), universe.end()), universe.end());
            //create the basic block order to be traversed (forwards)
            DenseMap<const BasicBlock*, BlockState> st;
            std::vector<BasicBlock*> order;
            order.push_back(&F.getEntryBlock());
            for (size_t i = 0; i < order.size(); ++i) {
                for (BasicBlock* succ : successors(order[i])) {
                    if (std::find(order.begin(), order.end(), succ) == order.end())
                        order.push_back(succ);
                }
            }
            BitVector all(universe.size(), true);
            //traverse the basic blocks in order
            for (BasicBlock* BB : order) {
                //set the bit vectors inital values (out = all expr, else empty)
                BlockState bs;
                bs.in = BitVector(universe.size(), false);
                bs.out = all;
                bs.gen = BitVector(universe.size(), false);
                bs.kill = BitVector(universe.size(), false);

                for (Instruction& I : *BB) {
                    //check if intr is binary operator
                    if (auto* BO = dyn_cast<BinaryOperator>(&I)) {
                        //add the expression to gen set
                        Expr e = Expr::fromBO(*BO);
                        auto it = std::lower_bound(universe.begin(), universe.end(), e);
                        if (it != universe.end() && *it == e) bs.gen.set(static_cast<unsigned>(it - universe.begin()));
                    }
                    //if the instruction is void, it cannot kill an expr
                    if (!I.getType()->isVoidTy()) {
                        //search for every expression that the intr def kills and kill it
                        for (size_t i = 0; i < universe.size(); ++i) {
                            if (universe[i].lhs == &I || universe[i].rhs == &I) bs.kill.set(static_cast<unsigned>(i));
                        }
                    }
                    //logic to ensure the gen overrides kill
                    BitVector notGen = bs.gen;
                    bs.kill &= notGen.flip();
                    st[BB] = bs;
                }
            }
            //run the algorithm until no more outs are changed
            bool changed = true;
            while (changed) {
                changed = false;

                for (BasicBlock* BB : order) {
                    std::vector<BitVector> predOuts;
                    if (BB == &F.getEntryBlock()) {
                        predOuts.push_back(BitVector(universe.size(), false));
                    }
                    for (BasicBlock* pred : predecessors(BB)) predOuts.push_back(st[pred].out);
                    if (predOuts.empty()) predOuts.push_back(BitVector(universe.size(), false));
                    st[BB].in = meetIntersect(predOuts);

                    //transfer function OUT = GEN U (IN - KILL).
                    //flip kill for bitvector subtraction
                    BitVector flipKill = st[BB].kill;
                    flipKill.flip();
                    //bitvector and (in, kill) to finish subtraction
                    BitVector inSub = st[BB].in;
                    inSub &= flipKill;
                    //bitvector or (gen, in - kill) for union
                    BitVector newOut = st[BB].gen;
                    newOut |= inSub;
                    //check if the out was changed
                    if (newOut != st[BB].out) {
                        st[BB].out = newOut;
                        changed = true;
                    }
                }
            }
            //prints all bit vectors for each block
            for (BasicBlock* BB : order) {

                outs() << "BB: ";
                BB->printAsOperand(outs(), false);
                outs() << "\n";
                printBitSet(outs(), "gen", st[BB].gen, universe);
                printBitSet(outs(), "kill", st[BB].kill, universe);
                printBitSet(outs(), "IN", st[BB].in, universe);
                printBitSet(outs(), "OUT", st[BB].out, universe);
            }

            return PreservedAnalyses::all();
        }
    };

    // -------------------- Liveness/Reaching (stubs) --------------------
    //Var structure
    struct Var {
        Value* var;
        auto operator<=>(const Var&) const = default;
    };
    //print a var
    raw_ostream& operator<<(raw_ostream& OS, const Var& V) {
        OS << getShortValueName(V.var);
        return OS;
    }
    struct LivenessPass : PassInfoMixin<LivenessPass> {
        struct BlockState {
            BitVector in;
            BitVector out;
            BitVector use;
            BitVector def;
        };
        //union between bit vectors
        static BitVector meetUnion(const std::vector<BitVector>& un) {
            if (un.empty()) return {};
            BitVector out = un[0];
            for (size_t i = 1; i < un.size(); ++i) out |= un[i];
            return out;
        }
        PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
            //print function name
            outs() << "=== ";
            F.printAsOperand(outs(), false);
            outs() << " ===\n";
            outs() << "[starter] liveness: implement backward dataflow (use/def, IN/OUT)\n";
            //determine all variables in the function
            std::vector<Var> universe;
            for (auto& BB : F) {
                for (auto& I : BB) {
                    //add vars (non-constants) to universe from operands
                    for (unsigned i = 0; i < I.getNumOperands(); ++i) {
                        Value* op = I.getOperand(i);
                        if (!isa<Constant>(op)) {
                            universe.push_back(Var{ op });
                        }
                    }
                    //Add def to universe unless instr is void
                    if (!I.getType()->isVoidTy()) {
                        universe.push_back(Var{ &I });
                    }

                }
            }
            //order and remove duplicates
            std::sort(universe.begin(), universe.end());
            universe.erase(std::unique(universe.begin(), universe.end()), universe.end());
            //create the basic block order to be traversed (backwards)
            DenseMap<const BasicBlock*, BlockState> st;
            std::vector<BasicBlock*> order;
            order.push_back(&F.back());
            for (size_t i = 0; i < order.size(); ++i) {
                for (BasicBlock* pred : predecessors(order[i])) {
                    if (std::find(order.begin(), order.end(), pred) == order.end())
                        order.push_back(pred);
                }
            }
            BitVector all(universe.size(), true);
            //traverse the basic blocks in reverse order
            for (BasicBlock* BB : order) {
                //set the bit vectors inital values (empty)
                BlockState bs;
                bs.in = BitVector(universe.size(), false);
                bs.out = BitVector(universe.size(), false);
                bs.use = BitVector(universe.size(), false);
                bs.def = BitVector(universe.size(), false);
                //traverse instructions in reverse order
                for (auto it = BB->rbegin(); it != BB->rend(); ++it) {
                    Instruction& I = *it;
                    //if the instruction is void, it cannot be a def
                    if (!I.getType()->isVoidTy()) {
                        Var v = Var{ &I };
                        auto it = std::lower_bound(universe.begin(), universe.end(), v);
                        if (it != universe.end() && *it == v) {
                            unsigned indVar = static_cast<unsigned>(it - universe.begin());
                            //add the var to the def set
                            bs.def.set(indVar);
                            //this is a prior def, so reset use
                            if (bs.use.test(indVar)) {
                                bs.use.reset(indVar);
                            }
                        }
                    }
                    //add operand vars to use set
                    for (unsigned i = 0; i < I.getNumOperands(); ++i) {
                        Value* op = I.getOperand(i);
                        //skip other block args, and other block phi labels
                        if (isa<Instruction>(op) || isa<Argument>(op)) {
                            //ensure var (non-constant)
                            if (!isa<Constant>(op)) {
                                Var v = Var{ op };
                                auto it = std::lower_bound(universe.begin(), universe.end(), v);
                                if (it != universe.end() && *it == v) {
                                    unsigned indVar = static_cast<unsigned>(it - universe.begin());
                                    //add var to the use set
                                    bs.use.set(indVar);
                                }
                            }
                        }
                    }

                }
                st[BB] = bs;
            }
            //run the algorithm until no more ins are changed
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock* BB : order) {
                    std::vector<BitVector> sucIns;
                    if (BB == &F.back()) sucIns.push_back(BitVector(universe.size(), false));
                    for (BasicBlock* suc : successors(BB)) sucIns.push_back(st[suc].in);
                    if (sucIns.empty()) sucIns.push_back(BitVector(universe.size(), false));
                    st[BB].out = meetUnion(sucIns);
                    //transfer function IN = USE U (OUT - DEF).
                    //flip def for bitvector subtraction
                    BitVector flipDef = st[BB].def;
                    flipDef.flip();
                    //bitvector and (out, def) to finish subtraction
                    BitVector outSub = st[BB].out;
                    outSub &= flipDef;
                    //bitvector or (use, out - def) for union
                    BitVector newIn = st[BB].use;
                    newIn |= outSub;
                    //check if the in was changed
                    if (newIn != st[BB].in) {
                        st[BB].in = newIn;
                        changed = true;
                    }
                }
            }
            //prints all bit vectors for each block
            for (auto it = order.rbegin(); it != order.rend(); ++it) {
                BasicBlock* BB = *it;
                outs() << "BB: ";
                BB->printAsOperand(outs(), false);
                outs() << "\n";
                printBitSet(outs(), "use", st[BB].use, universe);
                printBitSet(outs(), "def", st[BB].def, universe);
                printBitSet(outs(), "IN", st[BB].in, universe);
                printBitSet(outs(), "OUT", st[BB].out, universe);
            }
            return PreservedAnalyses::all();
        }
    };

    struct ReachingPass : PassInfoMixin<ReachingPass> {
        // reaching definition - (specific SSA value definition)
        // represent each def as a pointer to the defining Value (here: Instruction*),
        // and then map defs to a BitVector index so set ops are fast
        struct Def {
            Value* v;                          // points to the defining instruction (SSA value)
            auto operator<=>(const Def&) const = default; // used for sort/unique
        };

        // Pretty-print: definition as its SSA name, e.g., "%3", "%.01", "%add", etc
        friend raw_ostream& operator<<(raw_ostream& OS, const Def& D) {
            OS << getShortValueName(D.v);
            return OS;
        }

        // Per-basic-block dataflow state
        struct BlockState {
            BitVector in;   // IN/OUT are the usual reaching def sets at BB entry/exit
            BitVector out;
            BitVector gen;  // GEN = defs generated in this BB (definitions that occur in the block)
            BitVector kill; // KILL = defs invalidated by defs in this BB, SSA: intentionally empty
        };

        // Meet operator for Reaching Definitions (forward may-analysis):
        // IN[BB] = union of OUT[pred] over all predecessors
        static BitVector meetUnion(const std::vector<BitVector>& outs) {
            if (outs.empty()) return {};
            BitVector out = outs[0];
            for (size_t i = 1; i < outs.size(); ++i) out |= outs[i];
            return out;
        }

        PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
            outs() << "=== ";
            F.printAsOperand(outs(), false);
            outs() << " ===\n";

            // 1) Build the universe of facts (all possible reaching definitions)
            // after mem2reg, treat "definition" as any Instruction that produces a non-void SSA value
            std::vector<Def> universe;
            for (auto& BB : F) {
                for (auto& I : BB) {
                    if (!I.getType()->isVoidTy()) universe.push_back(Def{ &I });
                }
            }

            // Sort + unique --> deterministic indexing and stable printing.
            std::sort(universe.begin(), universe.end());
            universe.erase(std::unique(universe.begin(), universe.end()), universe.end());

            // 2) Choose a forward traversal order of reachable blocks
            // simple graph walk from entry to collect all reachable blocks
            // avoids iterating over unreachable blocks + gives deterministic printing order
            std::vector<BasicBlock*> order;
            order.push_back(&F.getEntryBlock());
            for (size_t i = 0; i < order.size(); ++i) {
                for (BasicBlock* succ : successors(order[i])) {
                    if (std::find(order.begin(), order.end(), succ) == order.end())
                        order.push_back(succ);
                }
            }

            DenseMap<const BasicBlock*, BlockState> st;

            // 3) Precompute GEN/KILL per basic block
            // GEN[BB] = all non-void instructions in BB (each defines an SSA value)
            // KILL[BB] = empty in SSA (no redefinitions of the same SSA value)
            for (BasicBlock* BB : order) {
                BlockState bs;
                bs.in = BitVector(universe.size(), false);
                bs.out = BitVector(universe.size(), false);
                bs.gen = BitVector(universe.size(), false);
                bs.kill = BitVector(universe.size(), false); // SSA kill is empty

                for (Instruction& I : *BB) {
                    if (I.getType()->isVoidTy()) continue;
                    Def d{ &I };
                    auto it = std::lower_bound(universe.begin(), universe.end(), d);
                    if (it != universe.end() && *it == d) {
                        bs.gen.set(static_cast<unsigned>(it - universe.begin()));
                    }
                }

                st[BB] = std::move(bs);
            }

            // 4) Fixed-point iteration (classic iterative dataflow solving)
            // Equations for Reaching Definitions (forward):
            //   IN[BB]  = ⋃ OUT[pred]
            //   OUT[BB] = GEN[BB] ∪ (IN[BB] − KILL[BB])
            bool changed = true;
            while (changed) {
                changed = false;

                for (BasicBlock* BB : order) {
                    // "No defs reaching before the Entry" :contentReference[oaicite:1]{index=1}
                    // --> treat entry as having an extra empty predecessor contribution
                    std::vector<BitVector> predOuts;
                    if (BB == &F.getEntryBlock()) {
                        predOuts.push_back(BitVector(universe.size(), false));
                    }
                    for (BasicBlock* pred : predecessors(BB)) predOuts.push_back(st[pred].out);
                    if (predOuts.empty()) predOuts.push_back(BitVector(universe.size(), false));

                    BitVector newIn = meetUnion(predOuts);

                    // Transfer:
                    // OUT = GEN ∪ (IN − KILL). Since KILL=∅, OUT = IN ∪ GEN
                    BitVector newOut = newIn;
                    newOut |= st[BB].gen;

                    // Update + continue iterating if anything changed
                    if (newIn != st[BB].in || newOut != st[BB].out) {
                        st[BB].in = std::move(newIn);
                        st[BB].out = std::move(newOut);
                        changed = true;
                    }
                }
            }

            // 5) Print output in required format
            //   BB Name
            //   gen [BB]
            //   kill [BB]
            //   IN [BB]
            //   OUT [BB]
            // :contentReference[oaicite:2]{index=2}
            for (BasicBlock* BB : order) {
                outs() << "BB: ";
                BB->printAsOperand(outs(), false);
                outs() << "\n";
                printBitSet(outs(), "gen", st[BB].gen, universe);
                printBitSet(outs(), "kill", st[BB].kill, universe);
                printBitSet(outs(), "IN", st[BB].in, universe);
                printBitSet(outs(), "OUT", st[BB].out, universe);
            }

            return PreservedAnalyses::all();
        }
    };

    // -------------------- Constant Propagation 3-point (starter) --------------------

    struct ConstantPropPass : PassInfoMixin<ConstantPropPass> {
        enum class Kind { Top, Const, Bottom };  // Top=unknown, Bottom=NAC

        struct LVal {
            Kind kind = Kind::Top;
            int64_t c = 0;
            //set LVal to unknown
            static LVal top() { return { Kind::Top, 0 }; }
            //set LVal to constant
            static LVal constant(int64_t v) { return { Kind::Const, v }; }
            //set LVal to NAC
            static LVal bottom() { return { Kind::Bottom, 0 }; }
            bool operator==(const LVal& o) const { return kind == o.kind && c == o.c; }
            bool operator!=(const LVal& o) const { return !(*this == o); }
        };

        using CPState = DenseMap<const Value*, LVal>;
        struct BlockState {
            CPState in;
            CPState out;
        };
        //the meet operation, var ^ unknown = var, NAC ^ var = NAC, const1 ^ const1 = const1, const1 ^ const2 = NAC
        static LVal meetVal(LVal a, LVal b) {
            if (a.kind == Kind::Top) return b;
            if (b.kind == Kind::Top) return a;
            if (a.kind == Kind::Bottom || b.kind == Kind::Bottom) return LVal::bottom();
            return (a.c == b.c) ? a : LVal::bottom();
        }
        //determine the dataflow value given the state
        static LVal evalValue(const Value* V, const CPState& st) {
            if (const auto* CI = dyn_cast<ConstantInt>(V)) return LVal::constant(CI->getSExtValue());
            auto it = st.find(V);
            if (it == st.end()) return LVal::top();
            return it->second;
        }
        //function to evaluate all binary operators
        static LVal evalBinary(const BinaryOperator& BO, const CPState& st) {
            LVal l = evalValue(BO.getOperand(0), st);
            LVal r = evalValue(BO.getOperand(1), st);
            if (l.kind == Kind::Bottom || r.kind == Kind::Bottom) return LVal::bottom();
            if (l.kind != Kind::Const || r.kind != Kind::Const) return LVal::top();
            // Starter example: only Add is implemented.
            // TODO(student): extend to Sub/Mul/Div and policy for unsupported ops.
            if (BO.getOpcode() == Instruction::Add) return LVal::constant(l.c + r.c);
            else if (BO.getOpcode() == Instruction::Sub) return LVal::constant(l.c - r.c);
            else if (BO.getOpcode() == Instruction::Mul) return LVal::constant(l.c * r.c);
            else if (BO.getOpcode() == Instruction::UDiv) return LVal::constant(l.c / r.c);
            //Return unknown if unsupported op
            return LVal::top();
        }
        static LVal evalPhi(const PHINode& Phi, const DenseMap<const BasicBlock*, BlockState>& states) {
            // TODO(student): merge incoming values from predecessor OUT states.
            LVal track = LVal::top();
            for (unsigned i = 0; i < Phi.getNumIncomingValues(); i++) {
                const BasicBlock* pred = Phi.getIncomingBlock(i);
                const Value* incVal = Phi.getIncomingValue(i);
                auto iter = states.find(pred);
                //if there are no predecessors the value is unknown
                if (iter == states.end()) {
                    return LVal::top();
                }
                const BlockState& predState = iter->second;
                //evaluate the predecessor out value with the incoming value
                LVal inc = evalValue(incVal, predState.out);
                //Perform meet operator with the currently tracked value and incoming value
                track = meetVal(track, inc);
                //If NAC exit, that is the bottom of the lattice
                if (track.kind == Kind::Bottom) {
                    return track;
                }
            }
            return track;
        }

        static CPState transferBlock(BasicBlock& BB, const CPState& in,
            const DenseMap<const BasicBlock*, BlockState>& states) {
            CPState out = in;
            for (Instruction& I : BB) {
                if (I.getType()->isVoidTy()) continue;

                if (auto* P = dyn_cast<PHINode>(&I)) {
                    out[&I] = evalPhi(*P, states);
                }
                else if (auto* BO = dyn_cast<BinaryOperator>(&I)) {
                    out[&I] = evalBinary(*BO, out);
                }
                else {
                    // TODO(student): handle icmp/select/loads/stores etc.
                        //check for integer comparison
                    if (auto IC = dyn_cast<ICmpInst>(&I)) {
                        LVal lhs = evalValue(IC->getOperand(0), out);
                        LVal rhs = evalValue(IC->getOperand(1), out);
                        //if either is NAC, that passes
                        if (lhs.kind == Kind::Bottom || rhs.kind == Kind::Bottom) {
                            out[&I] = LVal::bottom();
                        }
                        //if either is unknown, that passes
                        else if (lhs.kind == Kind::Top || rhs.kind == Kind::Top) {
                            out[&I] = LVal::top();
                        }
                        else {
                            //determine the result of every possible comparison of the constants
                            bool comparison;
                            bool defCase = false;
                            switch (IC->getPredicate()) {
                            case CmpInst::ICMP_EQ:
                                comparison = (lhs.c == rhs.c);
                                break;
                            case CmpInst::ICMP_NE:
                                comparison = (lhs.c != rhs.c);
                                break;
                            case CmpInst::ICMP_SLT:
                                comparison = (lhs.c < rhs.c);
                                break;
                            case CmpInst::ICMP_SLE:
                                comparison = (lhs.c <= rhs.c);
                                break;
                            case CmpInst::ICMP_SGT:
                                comparison = (lhs.c > rhs.c);
                                break;
                            case CmpInst::ICMP_SGE:
                                comparison = (lhs.c >= rhs.c);
                                break;
                            case CmpInst::ICMP_ULT:
                                comparison = (uint64_t)lhs.c < (uint64_t)rhs.c;
                                break;
                            case CmpInst::ICMP_ULE:
                                comparison = (uint64_t)lhs.c <= (uint64_t)rhs.c;
                                break;
                            case CmpInst::ICMP_UGT:
                                comparison = (uint64_t)lhs.c > (uint64_t)rhs.c;
                                break;
                            case CmpInst::ICMP_UGE:
                                comparison = (uint64_t)lhs.c >= (uint64_t)rhs.c;
                                break;
                            default:
                                out[&I] = LVal::top();
                                defCase = true;
                                break;
                            }
                            //skip comparison in the default case
                            if (!defCase) {
                                out[&I] = LVal::constant(comparison ? 1 : 0);
                            }
                        }
                    }
                    //a load is unknown
                    else if (isa<LoadInst>(&I)) {
                        out[&I] = LVal::top();
                    }
                    //a store has no result
                    else if (isa<StoreInst>(&I)) {
                        continue;
                    }
                    //check for select instruction
                    else if (auto* Sel = dyn_cast<SelectInst>(&I)) {
                        LVal cond = evalValue(Sel->getCondition(), out);
                        LVal tval = evalValue(Sel->getTrueValue(), out);
                        LVal fval = evalValue(Sel->getFalseValue(), out);

                        //check if cond is NAC
                        if (cond.kind == Kind::Bottom) {
                            out[&I] = LVal::bottom();
                        }
                        //if cond is unknown we can still check if tval and fval meet
                        else if (cond.kind == Kind::Top) {
                            out[&I] = meetVal(tval, fval);
                        }
                        //condition is const, we evaluate it and assign that as the new const
                        else {
                            if (cond.c == 1) {
                                out[&I] = tval;
                            }
                            else {
                                out[&I] = fval;
                            }
                        }
                    }
                    //case that assigns other operators to unknown
                    else {
                        out[&I] = LVal::top();
                    }
                }
            }
            return out;
        }

        static bool sameState(const CPState& a, const CPState& b, const std::vector<const Value*>& domain) {
            for (const Value* V : domain) {
                LVal av = a.lookup(V);
                LVal bv = b.lookup(V);
                if (av != bv) return false;
            }
            return true;
        }

        static void printState(raw_ostream& OS, StringRef label, const CPState& st,
            const std::vector<const Value*>& domain, bool showTop = false) {
            OS << "  " << label << ": { ";
            bool first = true;
            for (const Value* V : domain) {
                LVal v = st.lookup(V);
                if (!showTop && v.kind == Kind::Top) continue;
                if (!first) OS << "; ";
                first = false;
                V->printAsOperand(OS, false);
                if (v.kind == Kind::Const) OS << " = " << v.c;
                else if (v.kind == Kind::Bottom) OS << " = NAC";
                else OS << " = TOP";
            }
            OS << " }\n";
        }

        PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
            outs() << "=== ";
            F.printAsOperand(outs(), false);
            outs() << " ===\n";

            std::vector<const Value*> domain;
            for (auto& BB : F) {
                for (auto& I : BB) {
                    if (!I.getType()->isVoidTy()) domain.push_back(&I);
                }
            }

            std::vector<BasicBlock*> order;
            order.push_back(&F.getEntryBlock());
            for (size_t i = 0; i < order.size(); ++i) {
                for (BasicBlock* succ : successors(order[i])) {
                    if (std::find(order.begin(), order.end(), succ) == order.end()) order.push_back(succ);
                }
            }

            DenseMap<const BasicBlock*, BlockState> st;
            for (BasicBlock* BB : order) {
                BlockState bs;
                for (const Value* V : domain) {
                    bs.in[V] = LVal::top();
                    bs.out[V] = LVal::top();
                }
                st[BB] = std::move(bs);
            }
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock* BB : order) {
                    CPState newIn;
                    for (const Value* V : domain) newIn[V] = LVal::top();

                    bool hasPred = false;
                    for (BasicBlock* pred : predecessors(BB)) {
                        hasPred = true;
                        for (const Value* V : domain) newIn[V] = meetVal(newIn.lookup(V), st[pred].out.lookup(V));
                    }
                    if (!hasPred) {
                        for (const Value* V : domain) newIn[V] = LVal::top();
                    }
                    st[BB].in = newIn;
                    CPState newOut = transferBlock(*BB, newIn, st);

                    if (!sameState(st[BB].out, newOut, domain)) {
                        st[BB].out = std::move(newOut);
                        changed = true;
                    }
                }
            }

            for (BasicBlock* BB : order) {
                outs() << "BB: ";
                BB->printAsOperand(outs(), false);
                outs() << "\n";
                printState(outs(), "IN", st[BB].in, domain);
                printState(outs(), "OUT", st[BB].out, domain);
            }

            return PreservedAnalyses::all();
        }
    };
    // -------------------- Dominance --------------------
    //this is a structure that can be passed to LICM
    struct DominatorsResult {
        DenseMap<BasicBlock*, BitVector> in;
        DenseMap<BasicBlock*, BitVector> out;
        DenseMap<BasicBlock*, unsigned> index;
        std::vector<BasicBlock*> universe;
        bool dominates(BasicBlock* A, BasicBlock* B) const {
            unsigned aIdx = index.lookup(A);
            return in.lookup(B).test(aIdx);
        }
    };

    //print a bit vectors of basic blocks
    void printBitSet(raw_ostream& OS, StringRef label, const BitVector& bits, const std::vector<BasicBlock*>& universe) {
        OS << "  " << label << ": { ";
        bool first = true;
        for (unsigned i = 0; i < bits.size(); ++i) {
            if (!bits.test(i)) continue;
            if (!first) OS << "; ";
            first = false;

            if (universe[i]->hasName()) {
                OS << universe[i]->getName();
            }
            else {
                universe[i]->printAsOperand(OS, false);
            }
        }
        OS << " }\n";
    }
    struct DominatorsPass : public AnalysisInfoMixin<DominatorsPass> {
        static AnalysisKey Key;
        using Result = DominatorsResult;
        bool quiet = false;
        DominatorsPass(bool quiet = false) : quiet(quiet) {}
        struct BlockState {
            BitVector in;
            BitVector out;
        };
        //intersection between bit vectors
        static BitVector meetIntersect(const std::vector<BitVector>& ins) {
            if (ins.empty()) return {};
            BitVector out = ins[0];
            for (size_t i = 1; i < ins.size(); ++i) out &= ins[i];
            return out;
        }
        Result run(Function& F, FunctionAnalysisManager&) {
            
            if (!quiet) {
                F.printAsOperand(outs(), false);
                outs() << "\n";
                //print function name
                outs() << "=== ";
                F.printAsOperand(outs(), false);
                outs() << " ===\n";
            }
            //create set of all Basic Blocks
            DominatorsResult domRes;
            for (BasicBlock& BB : F)
                domRes.universe.push_back(&BB);
            //index map
            for (unsigned i = 0; i < domRes.universe.size(); ++i)
                domRes.index[domRes.universe[i]] = i;
            //init all
            DenseMap<BasicBlock*, BlockState> st;
            BitVector all(domRes.universe.size(), true);
            for (BasicBlock* BB : domRes.universe) {
                BlockState bs;
                bs.in = BitVector(domRes.universe.size(), false);
                bs.out = all;
                st[BB] = bs;
            }
            //run the algorithm until no more outs are changed
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock& BB : F) {
                    BasicBlock* BBptr = &BB;
                    std::vector<BitVector> predOuts;
                    if (BBptr == &F.getEntryBlock()) {
                        predOuts.push_back(BitVector(domRes.universe.size(), false));
                    }
                    //get preds
                    for (BasicBlock* pred : predecessors(BBptr)) {
                        predOuts.push_back(st[pred].out);
                    }
                    if (predOuts.empty()) {
                        predOuts.push_back(BitVector(domRes.universe.size(), false));
                    }
                    //transfer funciton OUT = BB U IN
                    BitVector newIn = meetIntersect(predOuts);
                    BitVector newOut = newIn;
                    //set ind of current BB
                    newOut.set(domRes.index.lookup(&BB));
                    //check if exit condition
                    if (newOut != st[BBptr].out) {
                        st[BBptr].in = newIn;
                        st[BBptr].out = newOut;
                        changed = true;
                    }
                }
            }
            if (!quiet) {
                //print all the basic blocks w/ bit vectors
                for (BasicBlock* BB : domRes.universe) {
                    outs() << "BB: ";
                    BB->printAsOperand(outs(), false);
                    outs() << "\n";
                    printBitSet(outs(), "IN", st[BB].in, domRes.universe);
                    printBitSet(outs(), "OUT", st[BB].out, domRes.universe);
                }
                //print dominator relations
                outs() << "\n=== Dominator Relations ===\n";
                for (BasicBlock* B : domRes.universe) {
                    unsigned bIdx = domRes.index.lookup(B);
                    for (BasicBlock* A : domRes.universe) {
                        unsigned aIdx = domRes.index.lookup(A);
                        if (A != B && st[B].in.test(aIdx)) {
                            //print dominator relationship
                            B->printAsOperand(outs(), false);
                            outs() << " is dominated by ";
                            A->printAsOperand(outs(), false);
                            outs() << "\n";
                        }
                    }
                }
            }
            //Move results to the dominator result
            for (BasicBlock* BB : domRes.universe) {
                domRes.in[BB] = st[BB].in;
                domRes.out[BB] = st[BB].out;
            }

            return domRes;
        }

    };
    AnalysisKey DominatorsPass::Key;
    //printer pass for testing dominators
    struct DominatorsPrinterPass : PassInfoMixin<DominatorsPrinterPass> {
        PreservedAnalyses run(Function& F, FunctionAnalysisManager& AM) {
            auto& Res = AM.getResult<DominatorsPass>(F);
            return PreservedAnalyses::all();
        }
    };
    // -------------------- Point-to Analysis --------------------
    //Andersen Method
    class AndersenPTA {
    public:
        Function& F;
        DenseMap<Value*, SmallPtrSet<Value*, 8>> points;
        DenseMap<Value*, SmallPtrSet<Value*, 8>> incl;
        DenseMap<Value*, SmallPtrSet<Value*, 8>> storeEdges;

        AndersenPTA(Function& F_) : F(F_) {}

        //add map showing a dest pointer inluding everything in the source pointer  
        void addInclude(Value* dst, Value* src) {
            incl[dst].insert(src);
        }

        //add value to map of direct pointers to memory
        void addPointsTo(Value* dst, Value* obj) {
            points[dst].insert(obj);
        }

        //run Andersen's Method
        void run() {
            //treat each object as its own argument
            for (Argument& A : F.args()) {
                if (A.getType()->isPointerTy()) {
                    addPointsTo(&A, &A);
                }
            }
            //add all types of pointers to the maps
            for (BasicBlock& BB : F) {
                for (Instruction& I : BB) {
                    //add pointer that allocates this memory to the direct pointer map (p = alloc)
                    if (auto* allocI = dyn_cast<AllocaInst>(&I)) {
                        addPointsTo(allocI, allocI);
                    }
                    //add pointer that points to the same object as another pointer to the include map (p = &q)
                    if (auto* bitI = dyn_cast<BitCastInst>(&I)) {
                        if (bitI->getOperand(0)->getType()->isPointerTy()) {
                            addInclude(bitI, bitI->getOperand(0));
                        }
                    }
                    //make the load map to the pointer it is loading (p = load q)
                    if (auto* loadI = dyn_cast<LoadInst>(&I)) {
                        Value* q = loadI->getPointerOperand();
                        addInclude(loadI, q);
                    }
                    //the stored pointer contains what ever p points to, add the edge to the storeEdges map (store p = q)
                    if (auto* storeI = dyn_cast<StoreInst>(&I)) {
                        Value* p = storeI->getValueOperand();
                        Value* q = storeI->getPointerOperand();
                        if (p->getType()->isPointerTy()) {
                            storeEdges[q].insert(p);
                        }
                    }
                }
            }
            //perform fixed-point aglorithm to determine each pointer's contents
            fixedPointAndersen();
        }
        //Andersen fixed-point algorithm
        void fixedPointAndersen() {
            bool changed = true;
            while (changed) {
                changed = false;
                //loop through all elements in the include map
                for (auto& entry : incl) {
                    //extract destination and its source set
                    Value* dst = entry.first;
                    auto& srcSet = entry.second;
                    for (Value* src : srcSet) {
                        //check and see if the destinations set that it points to needs to be updated
                        for (Value* obj : points[src]) {
                            if (!points[dst].contains(obj)) {
                                points[dst].insert(obj);
                                changed = true;
                            }
                        }
                    }
                }
                //loop through elements in the storeEdges map
                for (auto& entry : storeEdges) {
                    //extract pointer (q) and its set of all pointers stored into *q
                    Value* q = entry.first;
                    auto& pointSet = entry.second;
                    for (Value* p : pointSet) {
                        //iterate through each object in the points-to set of q
                        for (Value* obj : points[q]) {
                            //iterate through each object in the points-to set of p
                            for (Value* objPts : points[p]) {
                                //scan elements to see what was newly inserted if we need to continue fixed-point
                                if (points[obj].insert(objPts).second) {
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        //checks to see if the values alias
        bool mayAlias(Value* A, Value* B) {
            //no points-to info
            if (!points.count(A) || !points.count(B)) {
                return true;
            }
            for (Value* x : points[A]) {
                //check if points-to sets intersect
                if (points[B].contains(x)) {
                    return true;
                }
            }
            //no aliasing 
            return false;
        }
    };

    // Steensgaard Method
    class SteensgaardPTA {
    public:
        Function& F;

        // union-find over pointer equivalence classes
        DenseMap<Value*, Value*> parentMap;
        // maps each pointer rep to its pointee rep
        DenseMap<Value*, Value*> pointsToMap;
        // alloca nodes used as unique abstract memory locs
        SmallVector<Value*, 8> AbstractObjects;

        SteensgaardPTA(Function& F_) : F(F_) {}

        // find the representative for V's equivalence class
        Value* find(Value* V) {
            if (!parentMap.count(V)) {
                parentMap[V] = V;
                return V;
            }
            if (parentMap[V] == V) {
                return V;
            }

            parentMap[V] = find(parentMap[V]); // path compression
            return parentMap[V];
        }

        // merge the equivalence classes for A and B
        Value* unite(Value* A, Value* B) {
            Value* RepA = find(A);
            Value* RepB = find(B);

            if (RepA == RepB) {
                return RepA;
            }

            parentMap[RepB] = RepA;

            // if both reps had pointee info, unify the pointees
            bool HasPtsA = pointsToMap.count(RepA);
            bool HasPtsB = pointsToMap.count(RepB);

            if (HasPtsA && HasPtsB) {
                Value* NewPointee = unite(pointsToMap[RepA], pointsToMap[RepB]);
                pointsToMap[RepA] = NewPointee;
            }
            else if (!HasPtsA && HasPtsB) {
                pointsToMap[RepA] = pointsToMap[RepB];
            }

            return RepA;
        }

        Value* makeAbstractObject() {
            // create a unique dummy stack obj as an abstract memory loc
            // instruction is inserted into the func, so LLVM owns its lifetime
            auto* AbstractObj = new AllocaInst(
                Type::getInt8Ty(F.getContext()),
                0,
                "pta.abstract",
                F.getEntryBlock().getFirstNonPHIIt()
            );

            AbstractObjects.push_back(AbstractObj);
            return AbstractObj;
        }

        // create/return the abstract pointee obj for pointer P
        Value* getPointee(Value* P) {
            Value* RepP = find(P);

            if (!pointsToMap.count(RepP)) {
                // give each unknown pointer its own unique abstract obj
                pointsToMap[RepP] = makeAbstractObject();
            }

            return find(pointsToMap[RepP]);
        }

        // set P to point to Obj
        void setPointee(Value* P, Value* Obj) {
            Value* RepP = find(P);
            Value* RepObj = find(Obj);

            if (!pointsToMap.count(RepP)) {
                pointsToMap[RepP] = RepObj;
            }
            else {
                pointsToMap[RepP] = unite(pointsToMap[RepP], RepObj);
            }
        }

        void run() {
            // pointer arguments are known pointer vals
            // treat each as initially pointing to its own abstract obj
            for (Argument& Arg : F.args()) {
                if (Arg.getType()->isPointerTy()) {
                    find(&Arg);
                    Value* ArgObj = makeAbstractObject();
                    setPointee(&Arg, ArgObj);
                }
            }

            // flow-insensitive scan: process each instruction once
            for (BasicBlock& BB : F) {
                for (Instruction& I : BB) {

                    // alloca creates a fresh stack obj, result is a pointer to obj
                    if (auto* AllocaI = dyn_cast<AllocaInst>(&I)) {
                        find(AllocaI);
                        Value* AllocObj = makeAbstractObject();
                        setPointee(AllocaI, AllocObj);
                        continue;
                    }

                    // bitcast preserves pointer id
                    if (auto* BitcastI = dyn_cast<BitCastInst>(&I)) {
                        if (BitcastI->getType()->isPointerTy() &&
                            BitcastI->getOperand(0)->getType()->isPointerTy()) {
                            unite(BitcastI, BitcastI->getOperand(0));
                        }
                        continue;
                    }

                    // gep field-insensitive - derived from the base pointer
                    if (auto* GEPI = dyn_cast<GetElementPtrInst>(&I)) {
                        if (GEPI->getType()->isPointerTy() &&
                            GEPI->getPointerOperand()->getType()->isPointerTy()) {
                            unite(GEPI, GEPI->getPointerOperand());
                        }
                        continue;
                    }
                    // if x = load p and x is a pointer, then x receives the pointee of p
                    
                    if (auto* LoadI = dyn_cast<LoadInst>(&I)) {
                        if (LoadI->getType()->isPointerTy()) {
                            // models x = *p at the pointee level instead of unifying x with p
                            Value* Ptr = LoadI->getPointerOperand();
                            Value* Pointee = getPointee(Ptr);
                            unite(LoadI, Pointee);
                        }
                        continue;
                    }
                    // if store q, p and q is a pointer, then *p receives q
                    if (auto* StoreI = dyn_cast<StoreInst>(&I)) {
                        Value* StoredValue = StoreI->getValueOperand();
                        Value* StoreDest = StoreI->getPointerOperand();

                        if (StoredValue->getType()->isPointerTy()) {
                            // unifies the pointee of p with q, not p with q
                            Value* DestPointee = getPointee(StoreDest);
                            unite(DestPointee, StoredValue);
                        }
                        continue;
                    }
                }
            }
        }

        bool mayAlias(Value* A, Value* B) {
            if (!A || !B) {
                return true;
            }

            if (!A->getType()->isPointerTy() || !B->getType()->isPointerTy()) {
                return false;
            }

            if (!parentMap.count(A) || !parentMap.count(B)) {
                return true;
            }

            // pointer expressions may alias if their pointee reps match
            Value* PointeeA = getPointee(A);
            Value* PointeeB = getPointee(B);

            return find(PointeeA) == find(PointeeB);
        }
    };
    enum class PTAType {
        Andersen,
        Steensgaard,
        LICM
    };
    // -------------------- LICM --------------------
    struct LoopInvariantCodeMotion : PassInfoMixin<LoopInvariantCodeMotion> {    
        PTAType Mode;
        std::unique_ptr<AndersenPTA> Andersen;
        std::unique_ptr<SteensgaardPTA> Steensgaard;
        //Set mode for Andersen or Steensgaard
        LoopInvariantCodeMotion(PTAType M = PTAType::Andersen)
            : Mode(M) {}
        
        bool mayAlias(Value* A, Value* B) {
            if (Mode == PTAType::Andersen) {
                return Andersen->mayAlias(A, B);
            }
            else {
                return Steensgaard->mayAlias(A, B);
            }
        }
        bool dominates(const DominatorsPass::Result& Dom, BasicBlock* A, BasicBlock* B) const {
            unsigned indA = Dom.index.lookup(A);
            const BitVector& inB = Dom.in.lookup(B);
            return inB.test(indA);
        }
        //this fucntion fixes the PHI values that are affected from code motion
        static Value* remapHeaderPhi(Value* val, BasicBlock* header, BasicBlock* preheader) {
            if (auto* PN = dyn_cast<PHINode>(val)) {
                if (PN->getParent() == header) {
                    int ind = PN->getBasicBlockIndex(preheader);
                    assert(ind >= 0 && "Header PHI must have Preheader incoming");
                    return PN->getIncomingValue(ind);
                }
            }
            return val;
        }

        PreservedAnalyses run(Loop& L, LoopAnalysisManager& LAM, LoopStandardAnalysisResults& AR, LPMUpdater& U) {
            BasicBlock* preheader = L.getLoopPreheader();
            //if there is no preheader exit
            if (!preheader) {
                return PreservedAnalyses::all();
            }
            Function& F = *L.getHeader()->getParent();
            //run the desired points-to analysis
            if (Mode == PTAType::Andersen) {
                Andersen = std::make_unique<AndersenPTA>(F);
                Andersen->run();
            }
            else if(Mode == PTAType::Steensgaard) {
                Steensgaard = std::make_unique<SteensgaardPTA>(F);
                Steensgaard->run();
            }
            //get dominators for current loop
            FunctionAnalysisManager tempFAM;
            //set dominators to no print
            DominatorsPass DP(true);
            DominatorsPass::Result dom = DP.run(F, tempFAM);
            SmallPtrSet<Instruction*, 32> hoistInvariant;
            SmallPtrSet<Value*, 32> invariantValues;
            //args are invariant
            for (Argument& A : F.args()) {
                invariantValues.insert(&A);
            }
            //instr outside loop are invariant
            for (BasicBlock& BB : F) {
                if (!L.contains(&BB)) {
                    for (Instruction& I : BB) {
                        invariantValues.insert(&I);
                    }
                }
            }
            //fixed point algorithm to determine loop invariant instructions and hoistable instructions
            bool changed = true;
            while (changed) {
                changed = false;
                for (BasicBlock* BB : L.blocks()) {
                    for (Instruction& I : *BB) {
                        //check for instructions that are already in the invariant list or cannot be invariant
                        if (hoistInvariant.contains(&I) || I.isTerminator() || isa<PHINode>(&I)) {
                            continue;
                        }
                        //skip load instr in classic mode
                        if (Mode == PTAType::LICM && isa<LoadInst>(&I)) {
                            continue;
                        }
                        //if classic LICM skip load invariant
                        if (Mode != PTAType::LICM) {
                            //--------------------THIS IS FOR LOAD INVARIANT EXPRESSIONS-------------------
                            if (auto* loadI = dyn_cast<LoadInst>(&I)) {
                                Value* ptr = loadI->getPointerOperand();
                                //check if address is invarient, if not this is not loop-inv
                                if (!invariantValues.contains(ptr)) {
                                    continue;
                                }
                                bool killed = false;
                                //check all the instructions in the loop and compute what they store
                                for (BasicBlock* loopBB : L.blocks()) {
                                    for (Instruction& loopI : *loopBB) {
                                        if (auto* storeI = dyn_cast<StoreInst>(&loopI)) {
                                            //check and see if the store kills this pointer
                                            if (mayAlias(ptr, storeI->getPointerOperand())) {
                                                killed = true;
                                                break;
                                            }
                                        }
                                    }
                                    //exit loop when killed
                                    if (killed) {
                                        break;
                                    }
                                }
                                //if the instruciton has been killed skip to next
                                if (killed) {
                                    continue;
                                }
                                //the load must dominate all uses outside of the loop
                                bool safe = true;
                                for (User* U : I.users()) {
                                    //extract all uses
                                    Instruction* useI = dyn_cast<Instruction>(U);
                                    if (!useI) {
                                        continue;
                                    }
                                    //check if the instruction dominates the use
                                    BasicBlock* useBB = useI->getParent();
                                    if (!L.contains(useBB)) {
                                        if (!dominates(dom, I.getParent(), useBB)) {
                                            safe = false;
                                            break;
                                        }
                                    }
                                }
                                //if not safe, instruction is should not be hoisted
                                if (!safe) {
                                    continue;
                                }
                                //add loop invariant load to both lists
                                outs() << "Hoistable load detected: ";
                                loadI->print(outs());
                                outs() << "\n";
                                hoistInvariant.insert(loadI);
                                invariantValues.insert(loadI);
                                changed = true;
                                continue;
                            }
                        }
                        //exclude all store instructions beyond this point
                        if (I.mayHaveSideEffects()) {
                            continue;
                        }
                        //--------------------THIS IS FOR PURE COMPUTATION INVARIANT EXPRESSIONS-------------------
                        //check if the operand is an invariant operand
                        bool opInv = true;
                        for (Value* Op : I.operands()) {
                            //constants are always invariant so exit
                            if (isa<Constant>(Op)) {
                                continue;
                            }
                            //check dynamic instructions
                            if (Instruction* Def = dyn_cast<Instruction>(Op)) {
                                //if the op def is not invarient, instr not invarient
                                if (!invariantValues.contains(Def)) {
                                    opInv = false;
                                    break;
                                }
                                continue;
                            }
                            //check if operand is a function arg
                            if (isa<Argument>(Op)) {
                                continue;
                            }
                            //anything left is invariant
                            opInv = false;
                            break;
                        }
                        //check if the block dominates the use or has uses outside of loop
                        bool safe = true;
                        for (User* U : I.users()) {
                            Instruction* UseI = dyn_cast<Instruction>(U);
                            if (!UseI) {
                                continue;
                            }
                            //check if the use is outside of loop and not dominated
                            BasicBlock* UseBB = UseI->getParent();
                            if (!L.contains(UseBB)) {
                                if (!dominates(dom, I.getParent(), UseBB)) {
                                    safe = false;
                                    break;
                                }
                            }
                        }
                        //skip insert if failed use domination, use is outside of the loop and not dominated, or non invariant operand
                        if (!safe || !opInv) {
                            continue;
                        }
                        //add loop invariant to both lists
                        hoistInvariant.insert(&I);
                        invariantValues.insert(&I);
                        changed = true;
                    }
                }
            }
            //print list of Hoistable Instrucitons
            outs() << "Hoistable Loop-invariant instructions:\n";
            for (Instruction* I : hoistInvariant) {
                outs() << "  ";
                I->print(outs());
                outs() << "\n";
            }
            //create a vector that will be used to sort the hoistable instructions
            SmallVector<Instruction*, 32> toHoist;
            toHoist.reserve(hoistInvariant.size());
            for (Instruction* I : hoistInvariant)
                toHoist.push_back(I);

            //sort the instrucitons by dominance
            llvm::sort(toHoist, [&](Instruction* A, Instruction* B) {
                if (dominates(dom, A->getParent(), B->getParent())) {
                    return true;
                }
                if (dominates(dom, B->getParent(), A->getParent())) {
                    return false;
                }
                return A < B;
                }
            );

            //now hoist the sorted instructions in order
            for (Instruction* I : toHoist) {
                #pragma clang diagnostic push
                #pragma clang diagnostic ignored "-Wdeprecated-declarations"
                I->moveBefore(preheader->getTerminator());
                #pragma clang diagnostic pop
            }
            return PreservedAnalyses::none();
        }
    };

    // -------------------- Dead Code Elimination (DCE) --------------------
    struct DeadCodeEliminationPass : PassInfoMixin<DeadCodeEliminationPass> {
        // Root liveness condition: instruction is live if it...
        // affects control flow (terminators)/has side effects/required for debugging or exception handling
        static bool isRootLive(Instruction *I) {
            return I->isTerminator() || isa<DbgInfoIntrinsic>(I) || isa<LandingPadInst>(I) || I->mayHaveSideEffects();
        }

        PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
            outs() << "=== ";
            F.printAsOperand(outs(), false);
            outs() << " ===\n";

            // Liveness-based DCE (backward dataflow over instructions)
            //   - start from "root live" instructions (side effects, terminators)
            //   - propagate liveness backward through operands
            //   - any instruction not marked live -> is dead/removed

            DenseSet<Instruction*> liveSet;           // set of instructions proven live
            std::vector<Instruction*> worklist;       // worklist for backward propagation

            // 1) Seed: initialize live set with root-live instructions
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (isRootLive(&I)) {
                        if (liveSet.insert(&I).second) {
                            worklist.push_back(&I);
                        }
                    }
                }
            }

            // 2) Backward propagation: if an instruction is live...
            //    all instructions that define its operands are also live
            while (!worklist.empty()) {
            Instruction *I = worklist.back();
            worklist.pop_back();

                for (Value *Op : I->operands()) {
                    if (Instruction *Def = dyn_cast<Instruction>(Op)) {
                        if (liveSet.insert(Def).second) {
                            worklist.push_back(Def);
                        }
                    }
                }
            }

            // 3) Identify dead instructions: instructions not in liveSet + w/o side effects can be removed
            std::vector<Instruction*> deadList;

            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (!liveSet.count(&I) && !I.isTerminator() && !I.mayHaveSideEffects()) {
                        deadList.push_back(&I);
                    }
                }
            }

            // 4) Erase dead instructions (reverse order for safety)
            // follows LLVM's standard pattern for in-place IR mutation.
            //for (Instruction *I : ToErase)
            for (auto it = deadList.rbegin(); it != deadList.rend(); ++it) {
                Instruction *I = *it;
                outs() << "Removing Instruction: ";
                I->print(outs());
                outs() << "\n";
                I->eraseFromParent();
            }

            outs() << "Total dead instructions removed: " << deadList.size() << "\n\n";

            return PreservedAnalyses::none();
        }
    };
} //namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return { LLVM_PLUGIN_API_VERSION, "UnifiedPass", "v0.3-starter", [](PassBuilder& PB) {
            PB.registerAnalysisRegistrationCallback([](FunctionAnalysisManager& FAM) {
                    FAM.registerPass([] { return DominatorsPass(); });
                }
            );
            PB.registerPipelineParsingCallback([](StringRef Name, FunctionPassManager& FPM, ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    if (Name == "available") {
                      FPM.addPass(AvailablePass());
                      return true;
                    }
                    if (Name == "liveness") {
                      FPM.addPass(LivenessPass());
                      return true;
                    }
                    if (Name == "reaching") {
                      FPM.addPass(ReachingPass());
                      return true;
                    }
                    if (Name == "constantprop") {
                      FPM.addPass(ConstantPropPass());
                      return true;
                    }
                    if (Name == "dominators") {
                        FPM.addPass(DominatorsPrinterPass());
                        return true;
                    }
                    if (Name == "dead-code-elimination") {
                        FPM.addPass(DeadCodeEliminationPass());
                        return true;
                    }
                    return false;
                  }
            );
            PB.registerPipelineParsingCallback([](StringRef Name, LoopPassManager& LPM, ArrayRef<PassBuilder::PipelineElement>) -> bool {
                if (Name == "licm-andersen") {
                    LPM.addPass(LoopInvariantCodeMotion(PTAType::Andersen));
                    return true;
                }

                if (Name == "licm-steensgaard") {
                    LPM.addPass(LoopInvariantCodeMotion(PTAType::Steensgaard));
                    return true;
                }
                if (Name == "licm-classic") {
                    LPM.addPass(LoopInvariantCodeMotion(PTAType::LICM));
                    return true;
                }

                return false;
                }
            );

          }
    };
}
