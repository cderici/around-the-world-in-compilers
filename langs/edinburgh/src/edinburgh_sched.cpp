#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>

using namespace llvm;

namespace {

class EdinburghScheduler final : public GenericScheduler {
public:
  explicit EdinburghScheduler(const MachineSchedContext *C)
      : GenericScheduler(C) {}

  void dumpPolicy() const override {
    errs() << "Edinburgh scheduler: generic policy with critical-path bias\n";
    GenericScheduler::dumpPolicy();
  }

protected:
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                    SchedBoundary *Zone) const override {
    bool GenericWins = GenericScheduler::tryCandidate(Cand, TryCand, Zone);

    if (!TryCand.SU || !Cand.SU)
      return GenericWins;

    // very small policy: prefer nodes that are involved in more work downstream
    // (they have more dependency height)
    if (tryGreater(static_cast<int>(TryCand.SU->getHeight()),
                   static_cast<int>(Cand.SU->getHeight()), TryCand, Cand,
                   BotPathReduce))
      return true;

    return GenericWins;
  }
};

ScheduleDAGInstrs *createEdinburghMachineSched(MachineSchedContext *C) {
  errs() << "Edinburgh scheduler selected for " << C->MF->getName() << "\n";
  return new ScheduleDAGMILive(C, std::make_unique<EdinburghScheduler>(C));
}

MachineSchedRegistry
    EdinburghSchedRegistry("edinburgh",
                           "Edinburgh critical-path biased MIR scheduler",
                           createEdinburghMachineSched);

} // namespace
