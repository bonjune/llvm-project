#include "llvm/IR/PassManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
struct TrackPathsOpts {
  StringRef SourceFileName;
  uint32_t TargetLine;

  TrackPathsOpts(StringRef SourceFileName, uint32_t TargetLine)
      : SourceFileName(SourceFileName), TargetLine(TargetLine) {}
};

class TrackPathsPass : public PassInfoMixin<TrackPathsPass> {
private:
  llvm::StringRef SourceFileName;
  uint32_t TargetLine;

  // Stream for a file to write the report to
  std::unique_ptr<llvm::raw_fd_ostream> ReportStream;

public:
  TrackPathsPass(llvm::StringRef SourceFileName, uint32_t TargetLine)
      : SourceFileName(SourceFileName), TargetLine(TargetLine) {
    // Open the file to write the report to
    std::error_code EC;
    ReportStream = std::make_unique<llvm::raw_fd_ostream>(
        "report.txt", EC, llvm::sys::fs::OF_Text);
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // namespace llvm