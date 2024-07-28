#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

#include "llvm/Transforms/Instrumentation/TrackPaths.h"

#include <queue>
#include <unordered_map>
#include <vector>

using namespace llvm;

static uintptr_t GetBasicBlockID(BasicBlock *BB) {
  return reinterpret_cast<uintptr_t>(BB);
}

static bool InstrumentPaths(BasicBlock &Target, BasicBlock &Entry) {
  errs() << "Basic Block: \n";
  Target.dump();

  // Use BFS to collect paths from the entry block to the target block.
  // Track back from the target block.

  std::vector<std::vector<BasicBlock *>> Paths;
  std::queue<std::vector<BasicBlock *>> Queue;

  Queue.push({&Target});

  while (!Queue.empty()) {
    std::vector<BasicBlock *> Path = Queue.front();
    Queue.pop();

    BasicBlock *Current = Path.back();
    if (Current == &Entry) {
      std::reverse(Path.begin(), Path.end());
      Paths.push_back(Path);
      continue;
    }

    for (BasicBlock *Pred : predecessors(Current)) {
      std::vector<BasicBlock *> NewPath = Path;
      NewPath.push_back(Pred);
      Queue.push(NewPath);
    }
  }

  if (Paths.empty()) {
    return false;
  }

  // Print the ID of the basic blocks of the paths
  for (unsigned i = 0; i < Paths.size(); ++i) {
    errs() << "Path " << i << ":\n";
    for (BasicBlock *BB : Paths[i]) {
      errs() << GetBasicBlockID(BB) << "\n";
    }
  }

  FunctionCallee CoverageRecorder = Target.getModule()->getOrInsertFunction(
      "___optmuzz_coverage", Type::getVoidTy(Target.getContext()),
      Type::getInt64Ty(Target.getContext()));

  std::unordered_map<BasicBlock *, bool> Instrumented;

  // Instrument the basic blocks to print the path ID
  // Assume that it calls coverage function, and we feed the path ID
  // as an argument to the coverage function.
  for (unsigned i = 0; i < Paths.size(); ++i) {
    for (BasicBlock *BB : Paths[i]) {
      if (Instrumented[BB]) {
        continue;
      }
      Instrumented[BB] = true;
      errs() << "Instrumenting: " << GetBasicBlockID(BB) << "\n";
      Value *PathID = ConstantInt::get(Type::getInt64Ty(BB->getContext()),
                                       GetBasicBlockID(BB));
      // Instrument the basic block
      BB->getFirstInsertionPt();
      IRBuilder<> Builder(BB, BB->getFirstInsertionPt());
      Builder.CreateCall(CoverageRecorder, {PathID});
    }
  }

  return true;
}

static BasicBlock *FindTargetBlock(Function &F, uint32_t TargetLine) {
  errs() << "Function: " << F.getName() << "\n";

  for (auto &BB : F) {
    for (auto &I : BB) {
      DebugLoc Loc = I.getDebugLoc();

      if (Loc && Loc.getLine() == TargetLine) {
        errs() << "Target Block ID: " << GetBasicBlockID(&BB) << "\n";
        return &BB;
      }
    }
  }

  return nullptr;
}

PreservedAnalyses TrackPathsPass::run(Module &M, ModuleAnalysisManager &AM) {
  GlobalVariable *Annotations = M.getGlobalVariable("llvm.global.annotations");
  *ReportStream << M.getSourceFileName() << "\n";
  if (M.getSourceFileName() != SourceFileName) {
    return PreservedAnalyses::all();
  }

  if (!Annotations) {
    return PreservedAnalyses::all();
  }
  ConstantArray *CA = cast<ConstantArray>(Annotations->getInitializer());
  if (!CA) {
    return PreservedAnalyses::all();
  }

  // @llvm.global.annotations = appending global [1 x { ptr, ptr, ptr, i32,
  // ptr }] [{ ptr, ptr, ptr, i32, ptr } { ptr @_Z13this_functionv, ptr
  // @.str.2, ptr @.str.1, i32 10, ptr null }], section "llvm.metadata"
  for (unsigned i = 0, e = CA->getNumOperands(); i != e; ++i) {
    ConstantStruct *CS = cast<ConstantStruct>(CA->getOperand(i));
    Function *AnnotatedFunction =
        cast<Function>(CS->getOperand(0)->stripPointerCasts());
    if (AnnotatedFunction) {
      if (BasicBlock *TargetBlock =
              FindTargetBlock(*AnnotatedFunction, TargetLine)) {
        bool Instrumented =
            InstrumentPaths(*TargetBlock, AnnotatedFunction->getEntryBlock());
        if (Instrumented) {
          return PreservedAnalyses::none();
        }
        return PreservedAnalyses::all();
      }
    }
  }

  return PreservedAnalyses::all();
}
