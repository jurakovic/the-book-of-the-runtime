// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "jitpch.h"

#ifdef _MSC_VER
#pragma hdrstop
#endif

#include "lower.h" // for LowerRange()

// Flowgraph Optimization

//------------------------------------------------------------------------
// fgComputeReturnBlocks: Compute the set of BBJ_RETURN blocks.
//
// Initialize `fgReturnBlocks` to a list of the BBJ_RETURN blocks in the function.
//
void Compiler::fgComputeReturnBlocks()
{
    fgReturnBlocks = nullptr;

    for (BasicBlock* const block : Blocks())
    {
        // If this is a BBJ_RETURN block, add it to our list of all BBJ_RETURN blocks. This list is only
        // used to find return blocks.
        if (block->KindIs(BBJ_RETURN))
        {
            fgReturnBlocks = new (this, CMK_Reachability) BasicBlockList(block, fgReturnBlocks);
        }
    }

#ifdef DEBUG
    if (verbose)
    {
        printf("Return blocks:");
        if (fgReturnBlocks == nullptr)
        {
            printf(" NONE");
        }
        else
        {
            for (const BasicBlockList* bl = fgReturnBlocks; bl != nullptr; bl = bl->next)
            {
                printf(" " FMT_BB, bl->block->bbNum);
            }
        }
        printf("\n");
    }
#endif // DEBUG
}

//------------------------------------------------------------------------
// fgRemoveUnreachableBlocks: Remove unreachable blocks.
//
// Some blocks (marked with BBF_DONT_REMOVE) can't be removed even if unreachable, in which case they
// are converted to `throw` blocks. Internal throw helper blocks and the single return block (if any)
// are never considered unreachable.
//
// Arguments:
//   canRemoveBlock - Method that determines if a block can be removed or not. In earlier phases, it
//       relies on the reachability set. During final phase, it depends on the DFS walk of the flowgraph
//       and considering blocks that are not visited as unreachable.
//
// Return Value:
//    Return true if changes were made that may cause additional blocks to be removable.
//
// Notes:
//    Unreachable blocks removal phase happens twice.
//
//    During early phases RecomputeLoopInfo, the logic to determine if a block is reachable
//    or not is based on the reachability sets, and hence it must be computed and valid.
//
//    During late phase, all the reachable blocks from fgFirstBB are traversed and everything
//    else are marked as unreachable (with exceptions of handler/filter blocks). As such, it
//    is not dependent on the validity of reachability sets.
//
template <typename CanRemoveBlockBody>
bool Compiler::fgRemoveUnreachableBlocks(CanRemoveBlockBody canRemoveBlock)
{
    bool hasUnreachableBlocks = false;
    bool changed              = false;

    // Mark unreachable blocks with BBF_REMOVED.
    for (BasicBlock* const block : Blocks())
    {
        // Internal throw blocks are always reachable.
        if (fgIsThrowHlpBlk(block))
        {
            continue;
        }
        else if (block == genReturnBB)
        {
            // Don't remove statements for the genReturnBB block, as we might have special hookups there.
            // For example, the profiler hookup needs to have the "void GT_RETURN" statement
            // to properly set the info.compProfilerCallback flag.
            continue;
        }
        else if (block->HasFlag(BBF_DONT_REMOVE) && block->isEmpty() && block->KindIs(BBJ_THROW))
        {
            // We already converted a non-removable block to a throw; don't bother processing it again.
            continue;
        }
        else if (!canRemoveBlock(block))
        {
            continue;
        }

        // Remove all the code for the block
        fgUnreachableBlock(block);

        // Make sure that the block was marked as removed */
        noway_assert(block->HasFlag(BBF_REMOVED));

        if (block->HasFlag(BBF_DONT_REMOVE))
        {
            // Unmark the block as removed, clear BBF_INTERNAL, and set BBF_IMPORTED

            JITDUMP("Converting BBF_DONT_REMOVE block " FMT_BB " to BBJ_THROW\n", block->bbNum);

            // If the CALLFINALLY is being replaced by a throw, then the CALLFINALLYRET is unreachable.
            if (block->isBBCallFinallyPair())
            {
                BasicBlock* const leaveBlock = block->Next();
                fgPrepareCallFinallyRetForRemoval(leaveBlock);
            }

            // The successors may be unreachable after this change.
            changed |= block->NumSucc() > 0;

            block->RemoveFlags(BBF_REMOVED | BBF_INTERNAL);
            block->SetFlags(BBF_IMPORTED);
            block->SetKindAndTargetEdge(BBJ_THROW);
            block->bbSetRunRarely();
        }
        else
        {
            /* We have to call fgRemoveBlock next */
            hasUnreachableBlocks = true;
            changed              = true;
        }
    }

    if (hasUnreachableBlocks)
    {
        // Now remove the unreachable blocks: if we marked a block with BBF_REMOVED then we need to
        // call fgRemoveBlock() on it.
        BasicBlock* bNext;
        for (BasicBlock* block = fgFirstBB; block != nullptr; block = bNext)
        {
            if (block->HasFlag(BBF_REMOVED))
            {
                bNext = fgRemoveBlock(block, /* unreachable */ true);
            }
            else
            {
                bNext = block->Next();
            }
        }
    }

    return changed;
}

//-------------------------------------------------------------
// fgComputeDominators: Compute dominators
//
// Returns:
//    Suitable phase status.
//
PhaseStatus Compiler::fgComputeDominators()
{
    if (m_dfsTree == nullptr)
    {
        m_dfsTree = fgComputeDfs();
    }

    if (m_domTree == nullptr)
    {
        m_domTree = FlowGraphDominatorTree::Build(m_dfsTree);
    }

    bool anyHandlers = false;
    for (EHblkDsc* const HBtab : EHClauses(this))
    {
        if (HBtab->HasFilter())
        {
            BasicBlock* filter = HBtab->ebdFilter;
            if (m_dfsTree->Contains(filter))
            {
                filter->SetDominatedByExceptionalEntryFlag();
                anyHandlers = true;
            }
        }

        BasicBlock* handler = HBtab->ebdHndBeg;
        if (m_dfsTree->Contains(handler))
        {
            handler->SetDominatedByExceptionalEntryFlag();
            anyHandlers = true;
        }
    }

    if (anyHandlers)
    {
        assert(m_dfsTree->GetPostOrder(m_dfsTree->GetPostOrderCount() - 1) == fgFirstBB);
        // Now propagate dominator flag in reverse post-order, skipping first BB.
        // (This could walk the dominator tree instead, but this linear order
        // is more efficient to visit and still guarantees we see the
        // dominators before the dominated blocks).
        for (unsigned i = m_dfsTree->GetPostOrderCount() - 1; i != 0; i--)
        {
            BasicBlock* block = m_dfsTree->GetPostOrder(i - 1);
            assert(block->bbIDom != nullptr);
            if (block->bbIDom->IsDominatedByExceptionalEntryFlag())
            {
                block->SetDominatedByExceptionalEntryFlag();
            }
        }
    }

    return PhaseStatus::MODIFIED_NOTHING;
}

//-------------------------------------------------------------
// fgInitBlockVarSets: Initialize the per-block variable sets (used for liveness analysis).
//
// Notes:
//   Initializes:
//      bbVarUse, bbVarDef, bbLiveIn, bbLiveOut,
//      bbMemoryUse, bbMemoryDef, bbMemoryLiveIn, bbMemoryLiveOut,
//      bbScope
//
void Compiler::fgInitBlockVarSets()
{
    for (BasicBlock* const block : Blocks())
    {
        block->InitVarSets(this);
    }

    fgBBVarSetsInited = true;
}

//------------------------------------------------------------------------
// fgPostImportationCleanups: clean up flow graph after importation
//
// Returns:
//   suitable phase status
//
// Notes:
//
//  Find and remove any basic blocks that are useless (e.g. they have not been
//  imported because they are not reachable, or they have been optimized away).
//
//  Remove try regions where no blocks in the try were imported.
//  Update the end of try and handler regions where trailing blocks were not imported.
//  Update the start of try regions that were partially imported (OSR)
//
//  For OSR, add "step blocks" and conditional logic to ensure the path from
//  method entry to the OSR logical entry point always flows through the first
//  block of any enclosing try.
//
//  In particular, given a method like
//
//  S0;
//  try {
//      S1;
//      try {
//          S2;
//          for (...) {}  // OSR logical entry here
//      }
//  }
//
//  Where the Sn are arbitrary hammocks of code, the OSR logical entry point
//  would be in the middle of a nested try. We can't branch there directly
//  from the OSR method entry. So we transform the flow to:
//
//  _firstCall = 0;
//  goto pt1;
//  S0;
//  pt1:
//  try {
//      if (_firstCall == 0) goto pt2;
//      S1;
//      pt2:
//      try {
//          if (_firstCall == 0) goto pp;
//          S2;
//          pp:
//          _firstCall = 1;
//          for (...)
//      }
//  }
//
//  where the "state variable" _firstCall guides execution appropriately
//  from OSR method entry, and flow always enters the try blocks at the
//  first block of the try.
//
PhaseStatus Compiler::fgPostImportationCleanup()
{
    // Bail, if this is a failed inline
    //
    if (compDonotInline())
    {
        return PhaseStatus::MODIFIED_NOTHING;
    }

    if (compIsForInlining())
    {
        // Update type of return spill temp if we have gathered
        // better info when importing the inlinee, and the return
        // spill temp is single def.
        if (fgNeedReturnSpillTemp())
        {
            CORINFO_CLASS_HANDLE retExprClassHnd = impInlineInfo->retExprClassHnd;
            if (retExprClassHnd != nullptr)
            {
                LclVarDsc* returnSpillVarDsc = lvaGetDesc(lvaInlineeReturnSpillTemp);

                if ((returnSpillVarDsc->lvType == TYP_REF) && returnSpillVarDsc->lvSingleDef)
                {
                    lvaUpdateClass(lvaInlineeReturnSpillTemp, retExprClassHnd, impInlineInfo->retExprClassHndIsExact);
                }
            }
        }
    }

    BasicBlock* cur;
    BasicBlock* nxt;

    // If we remove any blocks, we'll have to do additional work
    unsigned removedBlks = 0;

    for (cur = fgFirstBB; cur != nullptr; cur = nxt)
    {
        // Get hold of the next block (in case we delete 'cur')
        nxt = cur->Next();

        // Should this block be removed?
        if (!cur->HasFlag(BBF_IMPORTED))
        {
            noway_assert(cur->isEmpty());

            if (ehCanDeleteEmptyBlock(cur))
            {
                JITDUMP(FMT_BB " was not imported, marking as removed (%d)\n", cur->bbNum, removedBlks);

                // Notify all successors that cur is no longer a pred.
                //
                // This may not be necessary once we have pred lists built before importation.
                // When we alter flow in the importer branch opts, we should be able to make
                // suitable updates there for blocks that we plan to keep.
                //
                for (BasicBlock* succ : cur->Succs(this))
                {
                    fgRemoveAllRefPreds(succ, cur);
                }

                cur->SetFlags(BBF_REMOVED);
                removedBlks++;

                // Drop the block from the list.
                //
                // We rely on the fact that this does not clear out
                // cur->bbNext or cur->bbPrev in the code that
                // follows.
                fgUnlinkBlockForRemoval(cur);
            }
            else
            {
                // We were prevented from deleting this block by EH
                // normalization. Mark the block as imported.
                cur->SetFlags(BBF_IMPORTED);
            }
        }
    }

    // If no blocks were removed, we're done.
    // Unless we are an OSR method with a try entry.
    //
    if ((removedBlks == 0) && !(opts.IsOSR() && fgOSREntryBB->hasTryIndex()))
    {
        return PhaseStatus::MODIFIED_NOTHING;
    }

    // Update all references in the exception handler table.
    //
    // We may have made the entire try block unreachable.
    // Check for this case and remove the entry from the EH table.
    //
    // For OSR, just the initial part of a try range may become
    // unreachable; if so we need to shrink the try range down
    // to the portion that was imported.
    unsigned  XTnum;
    EHblkDsc* HBtab;
    unsigned  delCnt = 0;

    // Walk the EH regions from inner to outer
    for (XTnum = 0, HBtab = compHndBBtab; XTnum < compHndBBtabCount; XTnum++, HBtab++)
    {
    AGAIN:

        // If start of a try region was not imported, then we either
        // need to trim the region extent, or remove the region
        // entirely.
        //
        // In normal importation, it is not valid to jump into the
        // middle of a try, so if the try entry was not imported, the
        // entire try can be removed.
        //
        // In OSR importation the entry patchpoint may be in the
        // middle of a try, and we need to determine how much of the
        // try ended up getting imported.  Because of backwards
        // branches we may end up importing the entire try even though
        // execution starts in the middle.
        //
        // Note it is common in both cases for the ends of trys (and
        // associated handlers) to end up not getting imported, so if
        // the try region is not removed, we always check if we need
        // to trim the ends.
        //
        if (HBtab->ebdTryBeg->HasFlag(BBF_REMOVED))
        {
            // Usual case is that the entire try can be removed.
            bool removeTryRegion = true;

            if (opts.IsOSR())
            {
                // For OSR we may need to trim the try region start.
                //
                // We rely on the fact that removed blocks have been snipped from
                // the main block list, but that those removed blocks have kept
                // their bbprev (and bbnext) links.
                //
                // Find the first unremoved block before the try entry block.
                //
                BasicBlock* const oldTryEntry  = HBtab->ebdTryBeg;
                BasicBlock*       tryEntryPrev = oldTryEntry->Prev();
                assert(tryEntryPrev != nullptr);
                while (tryEntryPrev->HasFlag(BBF_REMOVED))
                {
                    tryEntryPrev = tryEntryPrev->Prev();
                    // Because we've added an unremovable scratch block as
                    // fgFirstBB, this backwards walk should always find
                    // some block.
                    assert(tryEntryPrev != nullptr);
                }

                // If there is a next block of this prev block, and that block is
                // contained in the current try, we'd like to make that block
                // the new start of the try, and keep the region.
                BasicBlock* newTryEntry    = tryEntryPrev->Next();
                bool        updateTryEntry = false;

                if ((newTryEntry != nullptr) && bbInTryRegions(XTnum, newTryEntry))
                {
                    // We want to trim the begin extent of the current try region to newTryEntry.
                    //
                    // This method is invoked after EH normalization, so we may need to ensure all
                    // try regions begin at blocks that are not the start or end of some other try.
                    //
                    // So, see if this block is already the start or end of some other EH region.
                    if (bbIsTryBeg(newTryEntry))
                    {
                        // We've already end-trimmed the inner try. Do the same now for the
                        // current try, so it is easier to detect when they mutually protect.
                        // (we will call this again later, which is harmless).
                        fgSkipRmvdBlocks(HBtab);

                        // If this try and the inner try form a "mutually protected try region"
                        // then we must continue to share the try entry block.
                        EHblkDsc* const HBinner = ehGetBlockTryDsc(newTryEntry);
                        assert(HBinner->ebdTryBeg == newTryEntry);

                        if (HBtab->ebdTryLast != HBinner->ebdTryLast)
                        {
                            updateTryEntry = true;
                        }
                    }
                    // Also, a try and handler cannot start at the same block
                    else if (bbIsHandlerBeg(newTryEntry))
                    {
                        updateTryEntry = true;
                    }

                    if (updateTryEntry)
                    {
                        // We need to trim the current try to begin at a different block. Normally
                        // this would be problematic as we don't have enough context to redirect
                        // all the incoming edges, but we know oldTryEntry is unreachable.
                        // So there are no incoming edges to worry about.
                        //
                        assert(!tryEntryPrev->bbFallsThrough());

                        // What follows is similar to fgNewBBInRegion, but we can't call that
                        // here as the oldTryEntry is no longer in the main bb list.
                        newTryEntry = BasicBlock::New(this);
                        newTryEntry->SetFlags(BBF_IMPORTED | BBF_INTERNAL);
                        newTryEntry->bbRefs = 0;

                        // Set the right EH region indices on this new block.
                        //
                        // Patchpoints currently cannot be inside handler regions,
                        // and so likewise the old and new try region entries.
                        assert(!oldTryEntry->hasHndIndex());
                        newTryEntry->setTryIndex(XTnum);
                        newTryEntry->clearHndIndex();
                        fgInsertBBafter(tryEntryPrev, newTryEntry);

                        // Generally this (unreachable) empty new try entry block can fall through
                        // to the next block, but in cases where there's a nested try with an
                        // out of order handler, the next block may be a handler. So even though
                        // this new try entry block is unreachable, we need to give it a
                        // plausible flow target. Simplest is to just mark it as a throw.
                        if (bbIsHandlerBeg(newTryEntry->Next()))
                        {
                            newTryEntry->SetKindAndTargetEdge(BBJ_THROW);
                        }
                        else
                        {
                            FlowEdge* const newEdge = fgAddRefPred(newTryEntry->Next(), newTryEntry);
                            newTryEntry->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);
                        }

                        JITDUMP("OSR: changing start of try region #%u from " FMT_BB " to new " FMT_BB "\n",
                                XTnum + delCnt, oldTryEntry->bbNum, newTryEntry->bbNum);
                    }
                    else
                    {
                        // We can just trim the try to newTryEntry as it is not part of some inner try or handler.
                        JITDUMP("OSR: changing start of try region #%u from " FMT_BB " to " FMT_BB "\n", XTnum + delCnt,
                                oldTryEntry->bbNum, newTryEntry->bbNum);
                    }

                    // Update the handler table
                    fgSetTryBeg(HBtab, newTryEntry);

                    // Try entry blocks get specially marked and have special protection.
                    HBtab->ebdTryBeg->SetFlags(BBF_DONT_REMOVE);

                    // We are keeping this try region
                    removeTryRegion = false;
                }
            }

            if (removeTryRegion)
            {
                // In the dump, refer to the region by its original index.
                JITDUMP("Try region #%u (" FMT_BB " -- " FMT_BB ") not imported, removing try from the EH table\n",
                        XTnum + delCnt, HBtab->ebdTryBeg->bbNum, HBtab->ebdTryLast->bbNum);

                delCnt++;

                fgRemoveEHTableEntry(XTnum);

                if (XTnum < compHndBBtabCount)
                {
                    // There are more entries left to process, so do more. Note that
                    // HBtab now points to the next entry, that we copied down to the
                    // current slot. XTnum also stays the same.
                    goto AGAIN;
                }

                // no more entries (we deleted the last one), so exit the loop
                break;
            }
        }

        // If we get here, the try entry block was not removed.
        // Check some invariants.
        assert(HBtab->ebdTryBeg->HasFlag(BBF_IMPORTED));
        assert(HBtab->ebdTryBeg->HasFlag(BBF_DONT_REMOVE));
        assert(HBtab->ebdHndBeg->HasFlag(BBF_IMPORTED));
        assert(HBtab->ebdHndBeg->HasFlag(BBF_DONT_REMOVE));

        if (HBtab->HasFilter())
        {
            assert(HBtab->ebdFilter->HasFlag(BBF_IMPORTED));
            assert(HBtab->ebdFilter->HasFlag(BBF_DONT_REMOVE));
        }

        // Finally, do region end trimming -- update try and handler ends to reflect removed blocks.
        fgSkipRmvdBlocks(HBtab);
    }

    // If this is OSR, and the OSR entry was mid-try or in a nested try entry,
    // add the appropriate step block logic.
    //
    unsigned addedBlocks = 0;
    bool     addedTemps  = 0;

    if (opts.IsOSR())
    {
        BasicBlock* const osrEntry        = fgOSREntryBB;
        BasicBlock*       entryJumpTarget = osrEntry;

        if (osrEntry->hasTryIndex())
        {
            EHblkDsc*   enclosingTry   = ehGetBlockTryDsc(osrEntry);
            BasicBlock* tryEntry       = enclosingTry->ebdTryBeg;
            bool const  inNestedTry    = (enclosingTry->ebdEnclosingTryIndex != EHblkDsc::NO_ENCLOSING_INDEX);
            bool const  osrEntryMidTry = (osrEntry != tryEntry);

            if (inNestedTry || osrEntryMidTry)
            {
                JITDUMP("OSR Entry point at IL offset 0x%0x (" FMT_BB ") is %s%s try region EH#%u\n", info.compILEntry,
                        osrEntry->bbNum, osrEntryMidTry ? "within " : "at the start of ", inNestedTry ? "nested" : "",
                        osrEntry->getTryIndex());

                // We'll need a state variable to control the branching.
                //
                // It will be initialized to zero when the OSR method is entered and set to one
                // once flow reaches the osrEntry.
                //
                unsigned const entryStateVar   = lvaGrabTemp(false DEBUGARG("OSR entry state var"));
                lvaTable[entryStateVar].lvType = TYP_INT;
                addedTemps                     = true;

                // Zero the entry state at method entry.
                //
                GenTree* const initEntryState = gtNewTempStore(entryStateVar, gtNewZeroConNode(TYP_INT));
                fgNewStmtAtBeg(fgFirstBB, initEntryState);

                // Set the state variable once control flow reaches the OSR entry.
                //
                GenTree* const setEntryState = gtNewTempStore(entryStateVar, gtNewOneConNode(TYP_INT));
                fgNewStmtAtBeg(osrEntry, setEntryState);

                // Helper method to add flow
                //
                auto addConditionalFlow = [this, entryStateVar, &entryJumpTarget, &addedBlocks](BasicBlock* fromBlock,
                                                                                                BasicBlock* toBlock) {
                    BasicBlock* const newBlock = fgSplitBlockAtBeginning(fromBlock);
                    newBlock->inheritWeight(fromBlock);
                    fromBlock->SetFlags(BBF_INTERNAL);
                    newBlock->RemoveFlags(BBF_DONT_REMOVE);
                    addedBlocks++;
                    FlowEdge* const normalTryEntryEdge = fromBlock->GetTargetEdge();

                    GenTree* const entryStateLcl = gtNewLclvNode(entryStateVar, TYP_INT);
                    GenTree* const compareEntryStateToZero =
                        gtNewOperNode(GT_EQ, TYP_INT, entryStateLcl, gtNewZeroConNode(TYP_INT));
                    GenTree* const jumpIfEntryStateZero = gtNewOperNode(GT_JTRUE, TYP_VOID, compareEntryStateToZero);
                    fgNewStmtAtBeg(fromBlock, jumpIfEntryStateZero);

                    FlowEdge* const osrTryEntryEdge = fgAddRefPred(toBlock, fromBlock);
                    fromBlock->SetCond(osrTryEntryEdge, normalTryEntryEdge);

                    if (fgHaveProfileWeights())
                    {
                        // We are adding a path from (ultimately) the method entry to "fromBlock"
                        // Update the profile weight.
                        //
                        weight_t const entryWeight = fgFirstBB->bbWeight;

                        JITDUMP("Updating block weight for now-reachable try entry " FMT_BB " via " FMT_BB "\n",
                                fromBlock->bbNum, fgFirstBB->bbNum);
                        fromBlock->increaseBBProfileWeight(entryWeight);

                        // We updated the weight of fromBlock above.
                        //
                        // Set the likelihoods such that the additional weight flows to toBlock
                        // (and so the "normal path" profile out of fromBlock to newBlock is unaltered)
                        //
                        // In some stress cases we may have a zero-weight OSR entry.
                        // Tolerate this by capping the fromToLikelihood.
                        //
                        weight_t const fromWeight       = fromBlock->bbWeight;
                        weight_t const fromToLikelihood = min(1.0, entryWeight / fromWeight);

                        osrTryEntryEdge->setLikelihood(fromToLikelihood);
                        normalTryEntryEdge->setLikelihood(1.0 - fromToLikelihood);
                    }
                    else
                    {
                        // Just set likelihoods arbitrarily
                        //
                        osrTryEntryEdge->setLikelihood(0.9);
                        normalTryEntryEdge->setLikelihood(0.1);
                    }

                    entryJumpTarget = fromBlock;
                };

                // If this is a mid-try entry, add a conditional branch from the start of the try to osr entry point.
                //
                if (osrEntryMidTry)
                {
                    addConditionalFlow(tryEntry, osrEntry);
                }

                // Add conditional branches for each successive enclosing try with a distinct
                // entry block.
                //
                while (enclosingTry->ebdEnclosingTryIndex != EHblkDsc::NO_ENCLOSING_INDEX)
                {
                    EHblkDsc* const   nextTry      = ehGetDsc(enclosingTry->ebdEnclosingTryIndex);
                    BasicBlock* const nextTryEntry = nextTry->ebdTryBeg;

                    // We don't need to add flow for mutual-protect regions
                    // (multiple tries that all share the same entry block).
                    //
                    if (nextTryEntry != tryEntry)
                    {
                        addConditionalFlow(nextTryEntry, tryEntry);
                    }
                    enclosingTry = nextTry;
                    tryEntry     = nextTryEntry;
                }

                // Transform the method entry flow, if necessary.
                //
                // Note even if the OSR is in a nested try, if it's a mutual protect try
                // it can be reached directly from "outside".
                //
                assert(fgFirstBB->TargetIs(osrEntry));
                assert(fgFirstBB->KindIs(BBJ_ALWAYS));

                if (entryJumpTarget != osrEntry)
                {
                    fgRedirectTargetEdge(fgFirstBB, entryJumpTarget);

                    JITDUMP("OSR: redirecting flow from method entry " FMT_BB " to OSR entry " FMT_BB
                            " via step blocks.\n",
                            fgFirstBB->bbNum, fgOSREntryBB->bbNum);
                }
                else
                {
                    JITDUMP("OSR: leaving direct flow from method entry " FMT_BB " to OSR entry " FMT_BB
                            ", no step blocks needed.\n",
                            fgFirstBB->bbNum, fgOSREntryBB->bbNum);
                }
            }
            else
            {
                // If OSR entry is the start of an un-nested try, no work needed.
                //
                // We won't hit this case today as we don't allow the try entry to be the target of a backedge,
                // and currently patchpoints only appear at targets of backedges.
                //
                JITDUMP("OSR Entry point at IL offset 0x%0x (" FMT_BB
                        ") is start of an un-nested try region, no step blocks needed.\n",
                        info.compILEntry, osrEntry->bbNum);
                assert(entryJumpTarget == osrEntry);
                assert(fgOSREntryBB == osrEntry);
            }
        }
        else
        {
            // If OSR entry is not within a try, no work needed.
            //
            JITDUMP("OSR Entry point at IL offset 0x%0x (" FMT_BB ") is not in a try region, no step blocks needed.\n",
                    info.compILEntry, osrEntry->bbNum);
            assert(entryJumpTarget == osrEntry);
            assert(fgOSREntryBB == osrEntry);
        }
    }

#ifdef DEBUG
    fgVerifyHandlerTab();
#endif // DEBUG

    // Did we make any changes?
    //
    const bool madeChanges = (addedBlocks > 0) || (delCnt > 0) || (removedBlks > 0) || addedTemps;

    // Note that we have now run post importation cleanup,
    // so we can enable more stringent checking.
    //
    compPostImportationCleanupDone = true;

    return madeChanges ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}

//-------------------------------------------------------------
// fgCanCompactBlock: Determine if a BBJ_ALWAYS block and its target can be compacted.
//
// Arguments:
//    block - BBJ_ALWAYS block to check
//
// Returns:
//    true if compaction is allowed
//
bool Compiler::fgCanCompactBlock(BasicBlock* block)
{
    assert(block != nullptr);

    if (!block->KindIs(BBJ_ALWAYS) || block->HasFlag(BBF_KEEP_BBJ_ALWAYS))
    {
        return false;
    }

    BasicBlock* const target = block->GetTarget();

    if (block == target)
    {
        return false;
    }

    if (target->IsFirst() || (target == fgEntryBB) || (target == fgOSREntryBB))
    {
        return false;
    }

    // Don't bother compacting a call-finally pair if it doesn't succeed block
    //
    if (target->isBBCallFinallyPair() && !block->NextIs(target))
    {
        return false;
    }

    // If target has multiple incoming edges, we can still compact if block is empty.
    // However, not if it is the beginning of a handler.
    //
    if (target->countOfInEdges() != 1 &&
        (!block->isEmpty() || block->HasFlag(BBF_FUNCLET_BEG) || (block->bbCatchTyp != BBCT_NONE)))
    {
        return false;
    }

    if (target->HasFlag(BBF_DONT_REMOVE))
    {
        return false;
    }

    // Ensure we leave a valid init BB around.
    //
    if ((block == fgFirstBB) && !fgCanCompactInitBlock())
    {
        return false;
    }

    // We cannot compact two blocks in different EH regions.
    //
    if (!BasicBlock::sameEHRegion(block, target))
    {
        return false;
    }

    // If there is a switch predecessor don't bother because we'd have to update the uniquesuccs as well
    // (if they are valid).
    //
    for (BasicBlock* const predBlock : target->PredBlocks())
    {
        if (predBlock->KindIs(BBJ_SWITCH))
        {
            return false;
        }
    }

    return true;
}

//-------------------------------------------------------------
// fgCanCompactInitBlock: Check if the first BB (the init BB) can be compacted
// into its target.
//
// Returns:
//    true if compaction is allowed
//
bool Compiler::fgCanCompactInitBlock()
{
    assert(fgFirstBB->KindIs(BBJ_ALWAYS));
    BasicBlock* target = fgFirstBB->GetTarget();
    if (target->hasTryIndex())
    {
        // Inside a try region
        return false;
    }

    assert(target->bbPreds != nullptr);
    if (target->bbPreds->getNextPredEdge() != nullptr)
    {
        // Multiple preds
        return false;
    }

    if (opts.compDbgCode && !target->HasFlag(BBF_INTERNAL))
    {
        // Init BB must be internal for debug code to avoid conflating
        // JIT-inserted code with user code.
        return false;
    }

    return true;
}

//-------------------------------------------------------------
// fgCompactBlock: Compact BBJ_ALWAYS block and its target into one.
//
// Requires that all necessary checks have been performed, i.e. fgCanCompactBlock returns true.
//
// Uses for this function - whenever we change links, insert blocks, ...
// It will keep the flowgraph data in synch - bbNum, bbRefs, bbPreds
//
// Arguments:
//    block - move all code into this block from its target.
//
void Compiler::fgCompactBlock(BasicBlock* block)
{
    assert(fgCanCompactBlock(block));

    // We shouldn't churn the flowgraph after doing hot/cold splitting
    assert(fgFirstColdBlock == nullptr);

    BasicBlock* const target = block->GetTarget();

    JITDUMP("\nCompacting " FMT_BB " into " FMT_BB ":\n", target->bbNum, block->bbNum);
    fgRemoveRefPred(block->GetTargetEdge());

    if (target->countOfInEdges() > 0)
    {
        JITDUMP("Second block has %u other incoming edges\n", target->countOfInEdges());
        assert(block->isEmpty());

        // Retarget all the other edges incident on target
        for (BasicBlock* const predBlock : target->PredBlocksEditing())
        {
            fgReplaceJumpTarget(predBlock, target, block);
        }
    }

    assert(target->countOfInEdges() == 0);
    assert(target->bbPreds == nullptr);

    /* Start compacting - move all the statements in the second block to the first block */

    // First move any phi definitions of the second block after the phi defs of the first.
    // TODO-CQ: This may be the wrong thing to do. If we're compacting blocks, it's because a
    // control-flow choice was constant-folded away. So probably phi's need to go away,
    // as well, in favor of one of the incoming branches. Or at least be modified.

    assert(block->IsLIR() == target->IsLIR());
    if (block->IsLIR())
    {
        LIR::Range& blockRange  = LIR::AsRange(block);
        LIR::Range& targetRange = LIR::AsRange(target);

        // Does target have any phis?
        GenTree* targetNode = targetRange.FirstNode();

        // Does the block have any code?
        if (targetNode != nullptr)
        {
            LIR::Range targetNodes = targetRange.Remove(targetNode, targetRange.LastNode());
            blockRange.InsertAtEnd(std::move(targetNodes));
        }
    }
    else
    {
        Statement* blkNonPhi1    = block->FirstNonPhiDef();
        Statement* targetNonPhi1 = target->FirstNonPhiDef();
        Statement* blkFirst      = block->firstStmt();
        Statement* targetFirst   = target->firstStmt();

        // Does the second have any phis?
        if ((targetFirst != nullptr) && (targetFirst != targetNonPhi1))
        {
            Statement* targetLast = targetFirst->GetPrevStmt();
            assert(targetLast->GetNextStmt() == nullptr);

            // Does "blk" have phis?
            if (blkNonPhi1 != blkFirst)
            {
                // Yes, has phis.
                // Insert after the last phi of "block."
                // First, targetPhis after last phi of block.
                Statement* blkLastPhi = (blkNonPhi1 != nullptr) ? blkNonPhi1->GetPrevStmt() : blkFirst->GetPrevStmt();
                blkLastPhi->SetNextStmt(targetFirst);
                targetFirst->SetPrevStmt(blkLastPhi);

                // Now, rest of "block" after last phi of "target".
                Statement* targetLastPhi =
                    (targetNonPhi1 != nullptr) ? targetNonPhi1->GetPrevStmt() : targetFirst->GetPrevStmt();
                targetLastPhi->SetNextStmt(blkNonPhi1);

                if (blkNonPhi1 != nullptr)
                {
                    blkNonPhi1->SetPrevStmt(targetLastPhi);
                }
                else
                {
                    // block has no non phis, so make the last statement be the last added phi.
                    blkFirst->SetPrevStmt(targetLastPhi);
                }

                // Now update the bbStmtList of "target".
                target->bbStmtList = targetNonPhi1;
                if (targetNonPhi1 != nullptr)
                {
                    targetNonPhi1->SetPrevStmt(targetLast);
                }
            }
            else
            {
                if (blkFirst != nullptr) // If "block" has no statements, fusion will work fine...
                {
                    // First, targetPhis at start of block.
                    Statement* blkLast = blkFirst->GetPrevStmt();
                    block->bbStmtList  = targetFirst;
                    // Now, rest of "block" (if it exists) after last phi of "target".
                    Statement* targetLastPhi =
                        (targetNonPhi1 != nullptr) ? targetNonPhi1->GetPrevStmt() : targetFirst->GetPrevStmt();

                    targetFirst->SetPrevStmt(blkLast);
                    targetLastPhi->SetNextStmt(blkFirst);
                    blkFirst->SetPrevStmt(targetLastPhi);
                    // Now update the bbStmtList of "target"
                    target->bbStmtList = targetNonPhi1;
                    if (targetNonPhi1 != nullptr)
                    {
                        targetNonPhi1->SetPrevStmt(targetLast);
                    }
                }
            }
        }

        // Now proceed with the updated bbTreeLists.
        Statement* stmtList1 = block->firstStmt();
        Statement* stmtList2 = target->firstStmt();

        /* the block may have an empty list */

        if (stmtList1 != nullptr)
        {
            Statement* stmtLast1 = block->lastStmt();

            /* The second block may be a GOTO statement or something with an empty bbStmtList */
            if (stmtList2 != nullptr)
            {
                Statement* stmtLast2 = target->lastStmt();

                /* append list2 to list 1 */

                stmtLast1->SetNextStmt(stmtList2);
                stmtList2->SetPrevStmt(stmtLast1);
                stmtList1->SetPrevStmt(stmtLast2);
            }
        }
        else
        {
            /* block was formerly empty and now has target's statements */
            block->bbStmtList = stmtList2;
        }
    }

    // Transfer target's weight to block
    // (target's weight should include block's weight,
    // plus the weights of target's preds, which now flow into block)
    const bool hasProfileWeight = block->hasProfileWeight();
    block->inheritWeight(target);

    if (hasProfileWeight)
    {
        block->SetFlags(BBF_PROF_WEIGHT);
    }

    VarSetOps::AssignAllowUninitRhs(this, block->bbLiveOut, target->bbLiveOut);

    // Update the beginning and ending IL offsets (bbCodeOffs and bbCodeOffsEnd).
    // Set the beginning IL offset to the minimum, and the ending offset to the maximum, of the respective blocks.
    // If one block has an unknown offset, we take the other block.
    // We are merging into 'block', so if its values are correct, just leave them alone.
    // TODO: we should probably base this on the statements within.

    if (block->bbCodeOffs == BAD_IL_OFFSET)
    {
        block->bbCodeOffs = target->bbCodeOffs; // If they are both BAD_IL_OFFSET, this doesn't change anything.
    }
    else if (target->bbCodeOffs != BAD_IL_OFFSET)
    {
        // The are both valid offsets; compare them.
        if (block->bbCodeOffs > target->bbCodeOffs)
        {
            block->bbCodeOffs = target->bbCodeOffs;
        }
    }

    if (block->bbCodeOffsEnd == BAD_IL_OFFSET)
    {
        block->bbCodeOffsEnd = target->bbCodeOffsEnd; // If they are both BAD_IL_OFFSET, this doesn't change anything.
    }
    else if (target->bbCodeOffsEnd != BAD_IL_OFFSET)
    {
        // The are both valid offsets; compare them.
        if (block->bbCodeOffsEnd < target->bbCodeOffsEnd)
        {
            block->bbCodeOffsEnd = target->bbCodeOffsEnd;
        }
    }

    if (block->HasFlag(BBF_INTERNAL) && !target->HasFlag(BBF_INTERNAL))
    {
        // If 'block' is an internal block and 'target' isn't, then adjust the flags set on 'block'.
        block->RemoveFlags(BBF_INTERNAL); // Clear the BBF_INTERNAL flag
        block->SetFlags(BBF_IMPORTED);    // Set the BBF_IMPORTED flag
    }

    /* Update the flags for block with those found in target */

    block->CopyFlags(target, BBF_COMPACT_UPD);

    /* mark target as removed */

    target->SetFlags(BBF_REMOVED);

    /* Unlink target and update all the marker pointers if necessary */

    fgUnlinkRange(target, target);

    fgBBcount--;

    // If target was the last block of a try or handler, update the EH table.

    ehUpdateForDeletedBlock(target);

    /* Set the jump targets */

    switch (target->GetKind())
    {
        case BBJ_CALLFINALLY:
            // Propagate RETLESS property
            block->CopyFlags(target, BBF_RETLESS_CALL);

            FALLTHROUGH;

        case BBJ_ALWAYS:
        case BBJ_EHCATCHRET:
        case BBJ_EHFILTERRET:
        {
            /* Update the predecessor list for target's target */
            FlowEdge* const targetEdge = target->GetTargetEdge();
            fgReplacePred(targetEdge, block);

            block->SetKindAndTargetEdge(target->GetKind(), targetEdge);
            break;
        }

        case BBJ_COND:
        {
            /* Update the predecessor list for target's true target */
            FlowEdge* const trueEdge  = target->GetTrueEdge();
            FlowEdge* const falseEdge = target->GetFalseEdge();
            fgReplacePred(trueEdge, block);

            /* Update the predecessor list for target's false target if it is different from the true target */
            if (trueEdge != falseEdge)
            {
                fgReplacePred(falseEdge, block);
            }

            block->SetCond(trueEdge, falseEdge);
            break;
        }

        case BBJ_EHFINALLYRET:
            block->SetEhf(target->GetEhfTargets());
            fgChangeEhfBlock(target, block);
            break;

        case BBJ_EHFAULTRET:
        case BBJ_THROW:
        case BBJ_RETURN:
            /* no jumps or fall through blocks to set here */
            block->SetKind(target->GetKind());
            break;

        case BBJ_SWITCH:
            block->SetSwitch(target->GetSwitchTargets());
            // We are moving the switch jump from target to block. Examine the jump targets
            // of the BBJ_SWITCH at target and replace the predecessor to 'target' with ones to 'block'
            fgChangeSwitchBlock(target, block);
            break;

        default:
            noway_assert(!"Unexpected bbKind");
            break;
    }

    assert(block->KindIs(target->GetKind()));

#if DEBUG
    if (verbose && 0)
    {
        printf("\nAfter compacting:\n");
        fgDispBasicBlocks(false);
    }

    if (JitConfig.JitSlowDebugChecksEnabled() != 0)
    {
        // Make sure that the predecessor lists are accurate
        fgDebugCheckBBlist();
    }
#endif // DEBUG
}

//-------------------------------------------------------------
// fgUnreachableBlock: Remove a block when it is unreachable.
//
// This function cannot remove the first block.
//
// Arguments:
//    block - unreachable block to remove
//
void Compiler::fgUnreachableBlock(BasicBlock* block)
{
    // genReturnBB should never be removed, as we might have special hookups there.
    // Therefore, we should never come here to remove the statements in the genReturnBB block.
    // For example, the profiler hookup needs to have the "void GT_RETURN" statement
    // to properly set the info.compProfilerCallback flag.
    noway_assert(block != genReturnBB);

    if (block->HasFlag(BBF_REMOVED))
    {
        return;
    }

#ifdef DEBUG
    if (verbose)
    {
        printf("\nRemoving unreachable " FMT_BB "\n", block->bbNum);
    }
#endif // DEBUG

    noway_assert(!block->IsFirst()); // Can't use this function to remove the first block

    // First, delete all the code in the block.

    if (block->IsLIR())
    {
        LIR::Range& blockRange = LIR::AsRange(block);
        if (!blockRange.IsEmpty())
        {
            blockRange.Delete(this, block, blockRange.FirstNode(), blockRange.LastNode());
        }
    }
    else
    {
        // TODO-Cleanup: I'm not sure why this happens -- if the block is unreachable, why does it have phis?
        // Anyway, remove any phis.

        Statement* firstNonPhi = block->FirstNonPhiDef();
        if (block->bbStmtList != firstNonPhi)
        {
            if (firstNonPhi != nullptr)
            {
                firstNonPhi->SetPrevStmt(block->lastStmt());
            }
            block->bbStmtList = firstNonPhi;
        }

        for (Statement* const stmt : block->Statements())
        {
            fgRemoveStmt(block, stmt);
        }
        noway_assert(block->bbStmtList == nullptr);
    }

    // Mark the block as removed
    block->SetFlags(BBF_REMOVED);

    // Update bbRefs and bbPreds for the blocks reached by this block
    fgRemoveBlockAsPred(block);
}

//-------------------------------------------------------------
// fgOptimizeBranchToEmptyUnconditional:
//    Optimize a jump to an empty block which ends in an unconditional branch.
//
// Arguments:
//    block - source block
//    bDest - destination
//
// Returns: true if changes were made
//
bool Compiler::fgOptimizeBranchToEmptyUnconditional(BasicBlock* block, BasicBlock* bDest)
{
    bool optimizeJump = true;

    assert(bDest->isEmpty());
    assert(bDest->KindIs(BBJ_ALWAYS));

    // We do not optimize jumps between two different try regions.
    // However jumping to a block that is not in any try region is OK
    //
    if (bDest->hasTryIndex() && !BasicBlock::sameTryRegion(block, bDest))
    {
        optimizeJump = false;
    }

    // Don't optimize a jump to a removed block
    if (bDest->GetTarget()->HasFlag(BBF_REMOVED))
    {
        optimizeJump = false;
    }

    // Don't optimize a jump to a cloned finally
    if (bDest->HasFlag(BBF_CLONED_FINALLY_BEGIN))
    {
        optimizeJump = false;
    }

    // Must optimize jump if bDest has been removed
    //
    if (bDest->HasFlag(BBF_REMOVED))
    {
        optimizeJump = true;
    }

    if (optimizeJump)
    {
#ifdef DEBUG
        if (verbose)
        {
            printf("\nOptimizing a jump to an unconditional jump (" FMT_BB " -> " FMT_BB " -> " FMT_BB ")\n",
                   block->bbNum, bDest->bbNum, bDest->GetTarget()->bbNum);
        }
#endif // DEBUG

        weight_t removedWeight;

        // Optimize the JUMP to empty unconditional JUMP to go to the new target
        switch (block->GetKind())
        {
            case BBJ_ALWAYS:
            case BBJ_CALLFINALLYRET:
            {
                removedWeight = block->bbWeight;
                fgRedirectTargetEdge(block, bDest->GetTarget());
                break;
            }

            case BBJ_COND:
                if (block->TrueTargetIs(bDest))
                {
                    assert(!block->FalseTargetIs(bDest));
                    removedWeight = block->GetTrueEdge()->getLikelyWeight();
                    fgRedirectTrueEdge(block, bDest->GetTarget());
                }
                else
                {
                    assert(block->FalseTargetIs(bDest));
                    removedWeight = block->GetFalseEdge()->getLikelyWeight();
                    fgRedirectFalseEdge(block, bDest->GetTarget());
                }
                break;

            default:
                unreached();
        }

        //
        // When we optimize a branch to branch we need to update the profile weight
        // of bDest by subtracting out the weight of the path that is being optimized.
        //
        if (bDest->hasProfileWeight())
        {
            bDest->decreaseBBProfileWeight(removedWeight);
        }

        return true;
    }
    return false;
}

//-------------------------------------------------------------
// fgOptimizeEmptyBlock:
//   Does flow optimization of an empty block (can remove it in some cases)
//
// Arguments:
//    block - an empty block
//
// Returns: true if changes were made
//
bool Compiler::fgOptimizeEmptyBlock(BasicBlock* block)
{
    assert(block->isEmpty());

    // We shouldn't churn the flowgraph after doing hot/cold splitting
    assert(fgFirstColdBlock == nullptr);

    bool        madeChanges = false;
    BasicBlock* bPrev       = block->Prev();

    switch (block->GetKind())
    {
        case BBJ_COND:
        case BBJ_SWITCH:

            /* can never happen */
            noway_assert(!"Conditional or switch block with empty body!");
            break;

        case BBJ_THROW:
        case BBJ_CALLFINALLY:
        case BBJ_CALLFINALLYRET:
        case BBJ_RETURN:
        case BBJ_EHCATCHRET:
        case BBJ_EHFINALLYRET:
        case BBJ_EHFAULTRET:
        case BBJ_EHFILTERRET:

            /* leave them as is */
            /* some compilers generate multiple returns and put all of them at the end -
             * to solve that we need the predecessor list */

            break;

        case BBJ_ALWAYS:

            /* Special case for first BB */
            if (bPrev == nullptr)
            {
                assert(block == fgFirstBB);
                if (!block->JumpsToNext() || !fgCanCompactInitBlock())
                {
                    break;
                }
            }

            /* Do not remove a block that jumps to itself - used for while (true){} */
            if (block->TargetIs(block))
            {
                break;
            }

            // Don't remove the init BB if it does not leave a proper init BB
            // in place
            if ((block == fgFirstBB) && !fgCanCompactInitBlock())
            {
                break;
            }

            // Don't remove the fgEntryBB
            //
            if (opts.IsOSR() && (block == fgEntryBB))
            {
                break;
            }

            /* Don't remove an empty block that is in a different EH region
             * from its successor block, if the block is the target of a
             * catch return. It is required that the return address of a
             * catch be in the correct EH region, for re-raise of thread
             * abort exceptions to work. Insert a NOP in the empty block
             * to ensure we generate code for the block, if we keep it.
             */
            if (UsesFunclets())
            {
                BasicBlock* succBlock = block->GetTarget();

                if ((succBlock != nullptr) && !BasicBlock::sameEHRegion(block, succBlock))
                {
                    // The empty block and the block that follows it are in different
                    // EH regions. Is this a case where they can't be merged?

                    bool okToMerge = true; // assume it's ok
                    for (BasicBlock* const predBlock : block->PredBlocks())
                    {
                        if (predBlock->KindIs(BBJ_EHCATCHRET))
                        {
                            assert(predBlock->TargetIs(block));
                            okToMerge = false; // we can't get rid of the empty block
                            break;
                        }
                    }

                    if (!okToMerge)
                    {
                        // Insert a NOP in the empty block to ensure we generate code
                        // for the catchret target in the right EH region.
                        GenTree* nop = new (this, GT_NO_OP) GenTree(GT_NO_OP, TYP_VOID);

                        if (block->IsLIR())
                        {
                            LIR::AsRange(block).InsertAtEnd(nop);
                            LIR::ReadOnlyRange range(nop, nop);
                            m_pLowering->LowerRange(block, range);
                        }
                        else
                        {
                            Statement* nopStmt = fgNewStmtAtEnd(block, nop);
                            if (fgNodeThreading == NodeThreading::AllTrees)
                            {
                                fgSetStmtSeq(nopStmt);
                            }
                            gtSetStmtInfo(nopStmt);
                        }

                        madeChanges = true;

#ifdef DEBUG
                        if (verbose)
                        {
                            printf("\nKeeping empty block " FMT_BB " - it is the target of a catch return\n",
                                   block->bbNum);
                        }
#endif // DEBUG

                        break; // go to the next block
                    }
                }
            }

            if (!ehCanDeleteEmptyBlock(block))
            {
                // We're not allowed to remove this block due to reasons related to the EH table.
                break;
            }

            /* special case if this is the only BB */
            if (block->IsFirst() && block->IsLast())
            {
                assert(block == fgFirstBB);
                assert(block == fgLastBB);
                assert(bPrev == nullptr);
                break;
            }

            // When using profile weights, fgComputeCalledCount expects the first non-internal block to have profile
            // weight.
            // Make sure we don't break that invariant.
            if (fgIsUsingProfileWeights() && block->hasProfileWeight() && !block->HasFlag(BBF_INTERNAL))
            {
                BasicBlock* bNext = block->Next();

                // Check if the next block can't maintain the invariant.
                if ((bNext == nullptr) || bNext->HasFlag(BBF_INTERNAL) || !bNext->hasProfileWeight())
                {
                    // Check if the current block is the first non-internal block.
                    BasicBlock* curBB = bPrev;
                    while ((curBB != nullptr) && curBB->HasFlag(BBF_INTERNAL))
                    {
                        curBB = curBB->Prev();
                    }
                    if (curBB == nullptr)
                    {
                        // This block is the first non-internal block and it has profile weight.
                        // Don't delete it.
                        break;
                    }
                }
            }

            /* Remove the block */
            compCurBB = block;
            fgRemoveBlock(block, /* unreachable */ false);
            madeChanges = true;
            break;

        default:
            noway_assert(!"Unexpected bbKind");
            break;
    }

    return madeChanges;
}

//-------------------------------------------------------------
// fgOptimizeSwitchBranches:
//   Does flow optimization for a switch - bypasses jumps to empty unconditional branches,
//   and transforms degenerate switch cases like those with 1 or 2 targets.
//
// Arguments:
//    block - block with switch
//
// Returns: true if changes were made
//
bool Compiler::fgOptimizeSwitchBranches(BasicBlock* block)
{
    assert(block->KindIs(BBJ_SWITCH));

    unsigned    jmpCnt = block->GetSwitchTargets()->bbsCount;
    FlowEdge**  jmpTab = block->GetSwitchTargets()->bbsDstTab;
    BasicBlock* bNewDest; // the new jump target for the current switch case
    BasicBlock* bDest;
    bool        modified = false;

    do
    {
    REPEAT_SWITCH:;
        bDest    = (*jmpTab)->getDestinationBlock();
        bNewDest = bDest;

        // Do we have a JUMP to an empty unconditional JUMP block?
        if (bDest->isEmpty() && bDest->KindIs(BBJ_ALWAYS) && !bDest->TargetIs(bDest)) // special case for self jumps
        {
            bool optimizeJump = true;

            // We do not optimize jumps between two different try regions.
            // However jumping to a block that is not in any try region is OK
            //
            if (bDest->hasTryIndex() && !BasicBlock::sameTryRegion(block, bDest))
            {
                optimizeJump = false;
            }

            if (optimizeJump)
            {
                bNewDest = bDest->GetTarget();
#ifdef DEBUG
                if (verbose)
                {
                    printf("\nOptimizing a switch jump to an empty block with an unconditional jump (" FMT_BB
                           " -> " FMT_BB " -> " FMT_BB ")\n",
                           block->bbNum, bDest->bbNum, bNewDest->bbNum);
                }
#endif // DEBUG
            }
        }

        if (bNewDest != bDest)
        {
            //
            // When we optimize a branch to branch we need to update the profile weight
            // of bDest by subtracting out the block weight of the path that is being optimized.
            //
            FlowEdge* const oldEdge = *jmpTab;

            if (bDest->hasProfileWeight())
            {
                weight_t const branchThroughWeight = oldEdge->getLikelyWeight();
                bDest->decreaseBBProfileWeight(branchThroughWeight);
            }

            // Update the switch jump table
            fgRemoveRefPred(oldEdge);
            FlowEdge* const newEdge = fgAddRefPred(bNewDest, block, oldEdge);
            *jmpTab                 = newEdge;

            // Update edge likelihoods
            // Note old edge may still be "in use" so we decrease its likelihood.
            //

            // We want to move this much likelihood from old->new
            //
            const weight_t likelihoodFraction = oldEdge->getLikelihood() / (oldEdge->getDupCount() + 1);

            if (newEdge->getDupCount() == 1)
            {
                newEdge->setLikelihood(likelihoodFraction);
            }
            else
            {
                newEdge->addLikelihood(likelihoodFraction);
            }

            oldEdge->addLikelihood(-likelihoodFraction);

            // we optimized a Switch label - goto REPEAT_SWITCH to follow this new jump
            modified = true;

            goto REPEAT_SWITCH;
        }
    } while (++jmpTab, --jmpCnt);

    if (modified)
    {
        // Invalidate the set of unique targets for block, since we modified the targets
        fgInvalidateSwitchDescMapEntry(block);

        JITDUMP(
            "fgOptimizeSwitchBranches: Optimized switch flow. Profile needs to be re-propagated. Data %s consistent.\n",
            fgPgoConsistent ? "is now" : "was already");
        fgPgoConsistent = false;
    }

    Statement*  switchStmt = nullptr;
    LIR::Range* blockRange = nullptr;

    GenTree* switchTree;
    if (block->IsLIR())
    {
        blockRange = &LIR::AsRange(block);
        switchTree = blockRange->LastNode();

        assert(switchTree->OperGet() == GT_SWITCH_TABLE);
    }
    else
    {
        switchStmt = block->lastStmt();
        switchTree = switchStmt->GetRootNode();

        assert(switchTree->OperGet() == GT_SWITCH);
    }

    noway_assert(switchTree->gtType == TYP_VOID);

    // At this point all of the case jump targets have been updated such
    // that none of them go to block that is an empty unconditional block
    //
    jmpTab = block->GetSwitchTargets()->bbsDstTab;
    jmpCnt = block->GetSwitchTargets()->bbsCount;

    // Now check for two trivial switch jumps.
    //
    if (block->NumSucc(this) == 1)
    {
        // Use BBJ_ALWAYS for a switch with only a default clause, or with only one unique successor.

#ifdef DEBUG
        if (verbose)
        {
            printf("\nRemoving a switch jump with a single target (" FMT_BB ")\n", block->bbNum);
            printf("BEFORE:\n");
            fgDispBasicBlocks();
        }
#endif // DEBUG

        if (block->IsLIR())
        {
            bool               isClosed;
            unsigned           sideEffects;
            LIR::ReadOnlyRange switchTreeRange = blockRange->GetTreeRange(switchTree, &isClosed, &sideEffects);

            // The switch tree should form a contiguous, side-effect free range by construction. See
            // Lowering::LowerSwitch for details.
            assert(isClosed);
            assert((sideEffects & GTF_ALL_EFFECT) == 0);

            blockRange->Delete(this, block, std::move(switchTreeRange));
        }
        else
        {
            /* check for SIDE_EFFECTS */
            if (switchTree->gtFlags & GTF_SIDE_EFFECT)
            {
                /* Extract the side effects from the conditional */
                GenTree* sideEffList = nullptr;

                gtExtractSideEffList(switchTree, &sideEffList);

                if (sideEffList == nullptr)
                {
                    goto NO_SWITCH_SIDE_EFFECT;
                }

                noway_assert(sideEffList->gtFlags & GTF_SIDE_EFFECT);

#ifdef DEBUG
                if (verbose)
                {
                    printf("\nSwitch expression has side effects! Extracting side effects...\n");
                    gtDispTree(switchTree);
                    printf("\n");
                    gtDispTree(sideEffList);
                    printf("\n");
                }
#endif // DEBUG

                /* Replace the conditional statement with the list of side effects */
                noway_assert(sideEffList->gtOper != GT_SWITCH);

                switchStmt->SetRootNode(sideEffList);

                if (fgNodeThreading != NodeThreading::None)
                {
                    compCurBB = block;

                    /* Update ordering, costs, FP levels, etc. */
                    gtSetStmtInfo(switchStmt);

                    /* Re-link the nodes for this statement */
                    fgSetStmtSeq(switchStmt);
                }
            }
            else
            {

            NO_SWITCH_SIDE_EFFECT:

                /* conditional has NO side effect - remove it */
                fgRemoveStmt(block, switchStmt);
            }
        }

        // Change the switch jump into a BBJ_ALWAYS
        block->SetKindAndTargetEdge(BBJ_ALWAYS, block->GetSwitchTargets()->bbsDstTab[0]);
        for (unsigned i = 1; i < jmpCnt; ++i)
        {
            fgRemoveRefPred(jmpTab[i]);
        }

        return true;
    }
    else if (block->GetSwitchTargets()->bbsCount == 2)
    {
        /* Use a BBJ_COND(switchVal==0) for a switch with only one
           significant clause besides the default clause */
        GenTree* switchVal = switchTree->AsOp()->gtOp1;
        noway_assert(genActualTypeIsIntOrI(switchVal->TypeGet()));

        // If we are in LIR, remove the jump table from the block.
        if (block->IsLIR())
        {
            GenTree* jumpTable = switchTree->AsOp()->gtOp2;
            assert(jumpTable->OperGet() == GT_JMPTABLE);
            blockRange->Remove(jumpTable);
        }

        // Change the GT_SWITCH(switchVal) into GT_JTRUE(GT_EQ(switchVal==0)).
        // Also mark the node as GTF_DONT_CSE as further down JIT is not capable of handling it.
        // For example CSE could determine that the expression rooted at GT_EQ is a candidate cse and
        // replace it with a COMMA node.  In such a case we will end up with GT_JTRUE node pointing to
        // a COMMA node which results in noway asserts in fgMorphSmpOp(), optAssertionGen() and rpPredictTreeRegUse().
        // For the same reason fgMorphSmpOp() marks GT_JTRUE nodes with RELOP children as GTF_DONT_CSE.

#ifdef DEBUG
        if (verbose)
        {
            printf("\nConverting a switch (" FMT_BB ") with only one significant clause besides a default target to a "
                   "conditional branch. Before:\n",
                   block->bbNum);

            gtDispTree(switchTree);
        }
#endif // DEBUG

        switchTree->ChangeOper(GT_JTRUE);
        GenTree* zeroConstNode    = gtNewZeroConNode(genActualType(switchVal->TypeGet()));
        GenTree* condNode         = gtNewOperNode(GT_EQ, TYP_INT, switchVal, zeroConstNode);
        switchTree->AsOp()->gtOp1 = condNode;
        switchTree->AsOp()->gtOp1->gtFlags |= (GTF_RELOP_JMP_USED | GTF_DONT_CSE);

        if (block->IsLIR())
        {
            blockRange->InsertAfter(switchVal, zeroConstNode, condNode);
            LIR::ReadOnlyRange range(zeroConstNode, switchTree);
            m_pLowering->LowerRange(block, range);
        }
        else if (fgNodeThreading != NodeThreading::None)
        {
            gtSetStmtInfo(switchStmt);
            fgSetStmtSeq(switchStmt);
        }

        FlowEdge* const trueEdge  = block->GetSwitchTargets()->bbsDstTab[0];
        FlowEdge* const falseEdge = block->GetSwitchTargets()->bbsDstTab[1];
        block->SetCond(trueEdge, falseEdge);

        JITDUMP("After:\n");
        DISPNODE(switchTree);

        return true;
    }
    return modified;
}

//-------------------------------------------------------------
// fgBlockEndFavorsTailDuplication:
//     Heuristic function that returns true if this block ends in a statement that looks favorable
//     for tail-duplicating its successor (such as assigning a constant to a local).
//
//  Arguments:
//      block: BasicBlock we are considering duplicating the successor of
//      lclNum: local that is used by the successor block, provided by
//        prior call to fgBlockIsGoodTailDuplicationCandidate
//
//  Returns:
//     true if block end is favorable for tail duplication
//
//  Notes:
//     This is the second half of the evaluation for tail duplication, where we try
//     to determine if this predecessor block assigns a constant or provides useful
//     information about a local that is tested in an unconditionally executed successor.
//     If so then duplicating the successor will likely allow the test to be
//     optimized away.
//
bool Compiler::fgBlockEndFavorsTailDuplication(BasicBlock* block, unsigned lclNum)
{
    if (block->isRunRarely())
    {
        return false;
    }

    // If the local is address exposed, we currently can't optimize.
    //
    LclVarDsc* const lclDsc = lvaGetDesc(lclNum);

    if (lclDsc->IsAddressExposed())
    {
        return false;
    }

    Statement* const lastStmt  = block->lastStmt();
    Statement* const firstStmt = block->FirstNonPhiDef();

    if (lastStmt == nullptr)
    {
        return false;
    }

    // Tail duplication tends to pay off when the last statement
    // is a local store of a constant, arraylength, or a relop.
    // This is because these statements produce information about values
    // that would otherwise be lost at the upcoming merge point.
    //
    // Check up to N statements...
    //
    const int  limit = 2;
    int        count = 0;
    Statement* stmt  = lastStmt;

    while (count < limit)
    {
        count++;
        GenTree* const tree = stmt->GetRootNode();
        if (tree->OperIsLocalStore() && !tree->OperIsBlkOp() && (tree->AsLclVarCommon()->GetLclNum() == lclNum))
        {
            GenTree* const value = tree->Data();
            if (value->OperIsArrLength() || value->OperIsConst() || value->OperIsCompare())
            {
                return true;
            }
        }

        Statement* const prevStmt = stmt->GetPrevStmt();

        // The statement list prev links wrap from first->last, so exit
        // when we see lastStmt again, as we've now seen all statements.
        //
        if (prevStmt == lastStmt)
        {
            break;
        }

        stmt = prevStmt;
    }

    return false;
}

//-------------------------------------------------------------
// fgBlockIsGoodTailDuplicationCandidate:
//     Heuristic function that examines a block (presumably one that is a merge point) to determine
//     if it is a good candidate to be duplicated.
//
// Arguments:
//     target - the tail block (candidate for duplication)
//
// Returns:
//     true if this is a good candidate, false otherwise
//     if true, lclNum is set to lcl to scan for in predecessor block
//
// Notes:
//     The current heuristic is that tail duplication is deemed favorable if this
//     block simply tests the value of a local against a constant or some other local.
//
//     This is the first half of the evaluation for tail duplication. We subsequently
//     need to check if predecessors of this block assigns a constant to the local.
//
bool Compiler::fgBlockIsGoodTailDuplicationCandidate(BasicBlock* target, unsigned* lclNum)
{
    *lclNum = BAD_VAR_NUM;

    // Here we are looking for small blocks where a local live-into the block
    // ultimately feeds a simple conditional branch.
    //
    // These blocks are small, and when duplicated onto the tail of blocks that end in
    // local stores, there is a high probability of the branch completely going away.
    //
    // This is by no means the only kind of tail that it is beneficial to duplicate,
    // just the only one we recognize for now.
    if (!target->KindIs(BBJ_COND))
    {
        return false;
    }

    // No point duplicating this block if it's not a control flow join.
    if (target->bbRefs < 2)
    {
        return false;
    }

    // No point duplicating this block if it would not remove (part of) the join.
    if (target->TrueTargetIs(target) || target->FalseTargetIs(target))
    {
        return false;
    }

    Statement* const lastStmt  = target->lastStmt();
    Statement* const firstStmt = target->FirstNonPhiDef();

    // We currently allow just one statement aside from the branch.
    //
    if ((firstStmt != lastStmt) && (firstStmt != lastStmt->GetPrevStmt()))
    {
        return false;
    }

    // Verify the branch is just a simple local compare.
    //
    GenTree* const lastTree = lastStmt->GetRootNode();

    if (lastTree->gtOper != GT_JTRUE)
    {
        return false;
    }

    // must be some kind of relational operator
    GenTree* const cond = lastTree->AsOp()->gtOp1;
    if (!cond->OperIsCompare())
    {
        return false;
    }

    // op1 must be some combinations of casts of local or constant
    GenTree* op1 = cond->AsOp()->gtOp1;
    while (op1->gtOper == GT_CAST)
    {
        op1 = op1->AsOp()->gtOp1;
    }

    if (!op1->IsLocal() && !op1->OperIsConst())
    {
        return false;
    }

    // op2 must be some combinations of casts of local or constant
    GenTree* op2 = cond->AsOp()->gtOp2;
    while (op2->gtOper == GT_CAST)
    {
        op2 = op2->AsOp()->gtOp1;
    }

    if (!op2->IsLocal() && !op2->OperIsConst())
    {
        return false;
    }

    // Tree must have one constant and one local, or be comparing
    // the same local to itself.
    unsigned lcl1 = BAD_VAR_NUM;
    unsigned lcl2 = BAD_VAR_NUM;

    if (op1->IsLocal())
    {
        lcl1 = op1->AsLclVarCommon()->GetLclNum();
    }

    if (op2->IsLocal())
    {
        lcl2 = op2->AsLclVarCommon()->GetLclNum();
    }

    if ((lcl1 != BAD_VAR_NUM) && op2->OperIsConst())
    {
        *lclNum = lcl1;
    }
    else if ((lcl2 != BAD_VAR_NUM) && op1->OperIsConst())
    {
        *lclNum = lcl2;
    }
    else if ((lcl1 != BAD_VAR_NUM) && (lcl1 == lcl2))
    {
        *lclNum = lcl1;
    }
    else
    {
        return false;
    }

    // If there's no second statement, we're good.
    //
    if (firstStmt == lastStmt)
    {
        return true;
    }

    // Otherwise check the first stmt.
    // Verify the branch is just a simple local compare.
    //
    GenTree* const firstTree = firstStmt->GetRootNode();
    if (!firstTree->OperIs(GT_STORE_LCL_VAR))
    {
        return false;
    }

    unsigned storeLclNum = firstTree->AsLclVar()->GetLclNum();

    if (storeLclNum != *lclNum)
    {
        return false;
    }

    // Could allow unary here too...
    //
    GenTree* const data = firstTree->AsLclVar()->Data();
    if (!data->OperIsBinary())
    {
        return false;
    }

    // op1 must be some combinations of casts of local or constant
    // (or unary)
    op1 = data->AsOp()->gtOp1;
    while (op1->gtOper == GT_CAST)
    {
        op1 = op1->AsOp()->gtOp1;
    }

    if (!op1->IsLocal() && !op1->OperIsConst())
    {
        return false;
    }

    // op2 must be some combinations of casts of local or constant
    // (or unary)
    op2 = data->AsOp()->gtOp2;

    // A binop may not actually have an op2.
    //
    if (op2 == nullptr)
    {
        return false;
    }

    while (op2->gtOper == GT_CAST)
    {
        op2 = op2->AsOp()->gtOp1;
    }

    if (!op2->IsLocal() && !op2->OperIsConst())
    {
        return false;
    }

    // Tree must have one constant and one local, or be comparing
    // the same local to itself.
    lcl1 = BAD_VAR_NUM;
    lcl2 = BAD_VAR_NUM;

    if (op1->IsLocal())
    {
        lcl1 = op1->AsLclVarCommon()->GetLclNum();
    }

    if (op2->IsLocal())
    {
        lcl2 = op2->AsLclVarCommon()->GetLclNum();
    }

    if ((lcl1 != BAD_VAR_NUM) && op2->OperIsConst())
    {
        *lclNum = lcl1;
    }
    else if ((lcl2 != BAD_VAR_NUM) && op1->OperIsConst())
    {
        *lclNum = lcl2;
    }
    else if ((lcl1 != BAD_VAR_NUM) && (lcl1 == lcl2))
    {
        *lclNum = lcl1;
    }
    else
    {
        return false;
    }

    return true;
}

//-------------------------------------------------------------
// fgOptimizeUncondBranchToSimpleCond:
//    For a block which has an unconditional branch, look to see if its target block
//    is a good candidate for tail duplication, and if so do that duplication.
//
// Arguments:
//    block  - block with uncond branch
//    target - block which is target of first block
//
// Returns: true if changes were made
//
// Notes:
//   This optimization generally reduces code size and path length.
//
bool Compiler::fgOptimizeUncondBranchToSimpleCond(BasicBlock* block, BasicBlock* target)
{
    JITDUMP("Considering uncond to cond " FMT_BB " -> " FMT_BB "\n", block->bbNum, target->bbNum);

    if (!BasicBlock::sameEHRegion(block, target))
    {
        return false;
    }

    unsigned lclNum = BAD_VAR_NUM;

    // First check if the successor tests a local and then branches on the result
    // of a test, and obtain the local if so.
    //
    if (!fgBlockIsGoodTailDuplicationCandidate(target, &lclNum))
    {
        return false;
    }

    // At this point we know target is BBJ_COND.
    assert(target->KindIs(BBJ_COND));

    // Bail out if OSR, as we can have unusual flow into loops. If one
    // of target's successors is also a backedge target, this optimization
    // may mess up loop recognition by creating too many non-loop preds.
    //
    if (opts.IsOSR())
    {
        if (target->GetFalseTarget()->HasFlag(BBF_BACKWARD_JUMP_TARGET))
        {
            JITDUMP("Deferring: " FMT_BB " --> " FMT_BB "; latter looks like loop top\n", target->bbNum,
                    target->GetFalseTarget()->bbNum);
            return false;
        }

        if (target->GetTrueTarget()->HasFlag(BBF_BACKWARD_JUMP_TARGET))
        {
            JITDUMP("Deferring: " FMT_BB " --> " FMT_BB "; latter looks like loop top\n", target->bbNum,
                    target->GetTrueTarget()->bbNum);
            return false;
        }
    }

    // See if this block assigns constant or other interesting tree to that same local.
    //
    if (!fgBlockEndFavorsTailDuplication(block, lclNum))
    {
        return false;
    }

    // NOTE: we do not currently hit this assert because this function is only called when
    // `fgUpdateFlowGraph` has been called with `doTailDuplication` set to true, and the
    // backend always calls `fgUpdateFlowGraph` with `doTailDuplication` set to false.
    assert(!block->IsLIR());

    // Duplicate the target block at the end of this block
    //
    for (Statement* stmt : target->NonPhiStatements())
    {
        GenTree* clone = gtCloneExpr(stmt->GetRootNode());
        noway_assert(clone);
        Statement* cloneStmt = gtNewStmt(clone);

        if (fgNodeThreading != NodeThreading::None)
        {
            gtSetStmtInfo(cloneStmt);
        }

        fgInsertStmtAtEnd(block, cloneStmt);
    }

    // Fix up block's flow.
    // Assume edge likelihoods transfer over.
    //
    fgRedirectTargetEdge(block, target->GetTrueTarget());
    block->GetTargetEdge()->setLikelihood(target->GetTrueEdge()->getLikelihood());

    FlowEdge* const falseEdge = fgAddRefPred(target->GetFalseTarget(), block, target->GetFalseEdge());
    block->SetCond(block->GetTargetEdge(), falseEdge);

    JITDUMP("fgOptimizeUncondBranchToSimpleCond(from " FMT_BB " to cond " FMT_BB "), modified " FMT_BB "\n",
            block->bbNum, target->bbNum, block->bbNum);
    JITDUMP("   expecting opts to key off V%02u in " FMT_BB "\n", lclNum, block->bbNum);

    if (target->hasProfileWeight() && block->hasProfileWeight())
    {
        // Remove weight from target since block now bypasses it...
        //
        weight_t targetWeight = target->bbWeight;
        weight_t blockWeight  = block->bbWeight;
        target->decreaseBBProfileWeight(blockWeight);
        JITDUMP("Decreased " FMT_BB " profile weight from " FMT_WT " to " FMT_WT "\n", target->bbNum, targetWeight,
                target->bbWeight);
    }

    return true;
}

//-------------------------------------------------------------
// fgFoldSimpleCondByForwardSub:
//   Try to refine the flow of a block that may have just been tail duplicated
//   or compacted.
//
// Arguments:
//   block - block that was tail duplicated or compacted
//
// Returns Value:
//   true if control flow was changed
//
bool Compiler::fgFoldSimpleCondByForwardSub(BasicBlock* block)
{
    assert(block->KindIs(BBJ_COND));
    GenTree* jtrue = block->lastStmt()->GetRootNode();
    assert(jtrue->OperIs(GT_JTRUE));

    GenTree* relop = jtrue->gtGetOp1();
    if (!relop->OperIsCompare())
    {
        return false;
    }

    GenTree* op1 = relop->gtGetOp1();
    GenTree* op2 = relop->gtGetOp2();

    GenTree**            lclUse;
    GenTreeLclVarCommon* lcl;

    if (op1->OperIs(GT_LCL_VAR) && op2->IsIntegralConst())
    {
        lclUse = &relop->AsOp()->gtOp1;
        lcl    = op1->AsLclVarCommon();
    }
    else if (op2->OperIs(GT_LCL_VAR) && op1->IsIntegralConst())
    {
        lclUse = &relop->AsOp()->gtOp2;
        lcl    = op2->AsLclVarCommon();
    }
    else
    {
        return false;
    }

    Statement* secondLastStmt = block->lastStmt()->GetPrevStmt();
    if ((secondLastStmt == nullptr) || (secondLastStmt == block->lastStmt()))
    {
        return false;
    }

    GenTree* prevTree = secondLastStmt->GetRootNode();
    if (!prevTree->OperIs(GT_STORE_LCL_VAR))
    {
        return false;
    }

    GenTreeLclVarCommon* store = prevTree->AsLclVarCommon();
    if (store->GetLclNum() != lcl->GetLclNum())
    {
        return false;
    }

    if (!store->Data()->IsIntegralConst())
    {
        return false;
    }

    if (genActualType(store) != genActualType(store->Data()) || (genActualType(store) != genActualType(lcl)))
    {
        return false;
    }

    JITDUMP("Forward substituting local after jump threading. Before:\n");
    DISPSTMT(block->lastStmt());

    JITDUMP("\nAfter:\n");

    LclVarDsc* varDsc  = lvaGetDesc(lcl);
    GenTree*   newData = gtCloneExpr(store->Data());
    if (varTypeIsSmall(varDsc) && fgCastNeeded(store->Data(), varDsc->TypeGet()))
    {
        newData = gtNewCastNode(TYP_INT, newData, false, varDsc->TypeGet());
        newData = gtFoldExpr(newData);
    }

    *lclUse = newData;
    DISPSTMT(block->lastStmt());

    JITDUMP("\nNow trying to fold...\n");
    jtrue->AsUnOp()->gtOp1 = gtFoldExpr(relop);
    DISPSTMT(block->lastStmt());

    Compiler::FoldResult result = fgFoldConditional(block);
    if (result != Compiler::FoldResult::FOLD_DID_NOTHING)
    {
        assert(block->KindIs(BBJ_ALWAYS));
        return true;
    }

    return false;
}

//-------------------------------------------------------------
// fgRemoveConditionalJump:
//    Optimize a BBJ_COND block that unconditionally jumps to the same target
//
// Arguments:
//    block - BBJ_COND block with identical true/false targets
//
void Compiler::fgRemoveConditionalJump(BasicBlock* block)
{
    assert(block->KindIs(BBJ_COND));
    assert(block->TrueEdgeIs(block->GetFalseEdge()));

    BasicBlock* target = block->GetTrueTarget();

#ifdef DEBUG
    if (verbose)
    {
        printf("Block " FMT_BB " becoming a BBJ_ALWAYS to " FMT_BB " (jump target is the same whether the condition"
               " is true or false)\n",
               block->bbNum, target->bbNum);
    }
#endif // DEBUG

    if (block->IsLIR())
    {
        LIR::Range& blockRange = LIR::AsRange(block);
        GenTree*    jmp        = blockRange.LastNode();
        assert(jmp->OperIsConditionalJump());

        bool               isClosed;
        unsigned           sideEffects;
        LIR::ReadOnlyRange jmpRange;

        if (jmp->OperIs(GT_JCC))
        {
            // For JCC we have an invariant until resolution that the
            // previous node sets those CPU flags.
            GenTree* prevNode = jmp->gtPrev;
            assert((prevNode != nullptr) && ((prevNode->gtFlags & GTF_SET_FLAGS) != 0));
            prevNode->gtFlags &= ~GTF_SET_FLAGS;
            jmpRange = blockRange.GetTreeRange(prevNode, &isClosed, &sideEffects);
            jmpRange = LIR::ReadOnlyRange(jmpRange.FirstNode(), jmp);
        }
        else
        {
            jmpRange = blockRange.GetTreeRange(jmp, &isClosed, &sideEffects);
        }

        if (isClosed && ((sideEffects & GTF_SIDE_EFFECT) == 0))
        {
            // If the jump and its operands form a contiguous, side-effect-free range,
            // remove them.
            blockRange.Delete(this, block, std::move(jmpRange));
        }
        else
        {
            // Otherwise, just remove the jump node itself.
            blockRange.Remove(jmp, true);
        }
    }
    else
    {
        Statement* condStmt = block->lastStmt();
        GenTree*   cond     = condStmt->GetRootNode();
        noway_assert(cond->gtOper == GT_JTRUE);

        /* check for SIDE_EFFECTS */
        if (cond->gtFlags & GTF_SIDE_EFFECT)
        {
            /* Extract the side effects from the conditional */
            GenTree* sideEffList = nullptr;

            gtExtractSideEffList(cond, &sideEffList);

            if (sideEffList == nullptr)
            {
                compCurBB = block;
                fgRemoveStmt(block, condStmt);
            }
            else
            {
                noway_assert(sideEffList->gtFlags & GTF_SIDE_EFFECT);
#ifdef DEBUG
                if (verbose)
                {
                    printf("\nConditional has side effects! Extracting side effects...\n");
                    gtDispTree(cond);
                    printf("\n");
                    gtDispTree(sideEffList);
                    printf("\n");
                }
#endif // DEBUG

                /* Replace the conditional statement with the list of side effects */
                noway_assert(sideEffList->gtOper != GT_JTRUE);

                condStmt->SetRootNode(sideEffList);

                if (fgNodeThreading == NodeThreading::AllTrees)
                {
                    compCurBB = block;

                    /* Update ordering, costs, FP levels, etc. */
                    gtSetStmtInfo(condStmt);

                    /* Re-link the nodes for this statement */
                    fgSetStmtSeq(condStmt);
                }
            }
        }
        else
        {
            compCurBB = block;
            /* conditional has NO side effect - remove it */
            fgRemoveStmt(block, condStmt);
        }
    }

    /* Conditional is gone - always jump to target */

    block->SetKindAndTargetEdge(BBJ_ALWAYS, block->GetTrueEdge());
    assert(block->TargetIs(target));

    /* Update bbRefs and bbNum - Conditional predecessors to the same
     * block are counted twice so we have to remove one of them */

    noway_assert(target->countOfInEdges() > 1);
    fgRemoveRefPred(block->GetTargetEdge());
}

//-------------------------------------------------------------
// fgOptimizeBranch: Optimize an unconditional branch that branches to a conditional branch.
//
// Currently we require that the conditional branch jump back to the block that follows the unconditional
// branch. We can improve the code execution and layout by concatenating a copy of the conditional branch
// block at the end of the conditional branch and reversing the sense of the branch.
//
// This is only done when the amount of code to be copied is smaller than our calculated threshold
// in maxDupCostSz.
//
// Arguments:
//    bJump - block with branch
//
// Returns: true if changes were made
//
bool Compiler::fgOptimizeBranch(BasicBlock* bJump)
{
    if (opts.MinOpts())
    {
        return false;
    }

    if (!bJump->KindIs(BBJ_ALWAYS))
    {
        return false;
    }

    // We might be able to compact blocks that always jump to the next block.
    if (bJump->JumpsToNext())
    {
        return false;
    }

    if (bJump->HasFlag(BBF_KEEP_BBJ_ALWAYS))
    {
        return false;
    }

    BasicBlock* bDest = bJump->GetTarget();

    if (!bDest->KindIs(BBJ_COND))
    {
        return false;
    }

    if (!bJump->NextIs(bDest->GetTrueTarget()))
    {
        return false;
    }

    // 'bJump' must be in the same try region as the condition, since we're going to insert
    // a duplicated condition in 'bJump', and the condition might include exception throwing code.
    if (!BasicBlock::sameTryRegion(bJump, bDest))
    {
        return false;
    }

    // do not jump into another try region
    BasicBlock* bDestNormalTarget = bDest->GetFalseTarget();
    if (bDestNormalTarget->hasTryIndex() && !BasicBlock::sameTryRegion(bJump, bDestNormalTarget))
    {
        return false;
    }

    // This function is only called by fgReorderBlocks, which we do not run in the backend.
    // If we wanted to run block reordering in the backend, we would need to be able to
    // calculate cost information for LIR on a per-node basis in order for this function
    // to work.
    assert(!bJump->IsLIR());
    assert(!bDest->IsLIR());

    unsigned estDupCostSz = 0;
    for (Statement* const stmt : bDest->Statements())
    {
        // We want to compute the costs of the statement. Unfortunately, gtPrepareCost() / gtSetStmtInfo()
        // call gtSetEvalOrder(), which can reorder nodes. If it does so, we need to re-thread the gtNext/gtPrev
        // links. We don't know if it does or doesn't reorder nodes, so we end up always re-threading the links.

        gtSetStmtInfo(stmt);
        if (fgNodeThreading == NodeThreading::AllTrees)
        {
            fgSetStmtSeq(stmt);
        }

        GenTree* expr = stmt->GetRootNode();
        estDupCostSz += expr->GetCostSz();
    }

    bool     allProfileWeightsAreValid = false;
    weight_t weightJump                = bJump->bbWeight;
    weight_t weightDest                = bDest->bbWeight;
    weight_t weightNext                = bJump->Next()->bbWeight;
    bool     rareJump                  = bJump->isRunRarely();
    bool     rareDest                  = bDest->isRunRarely();
    bool     rareNext                  = bJump->Next()->isRunRarely();

    // If we have profile data then we calculate the number of time
    // the loop will iterate into loopIterations
    if (fgIsUsingProfileWeights())
    {
        // Only rely upon the profile weight when all three of these blocks
        // have either good profile weights or are rarelyRun
        //
        if (bJump->HasAnyFlag(BBF_PROF_WEIGHT | BBF_RUN_RARELY) &&
            bDest->HasAnyFlag(BBF_PROF_WEIGHT | BBF_RUN_RARELY) &&
            bJump->Next()->HasAnyFlag(BBF_PROF_WEIGHT | BBF_RUN_RARELY))
        {
            allProfileWeightsAreValid = true;

            if ((weightJump * 100) < weightDest)
            {
                rareJump = true;
            }

            if ((weightNext * 100) < weightDest)
            {
                rareNext = true;
            }

            if (((weightDest * 100) < weightJump) && ((weightDest * 100) < weightNext))
            {
                rareDest = true;
            }
        }
    }

    unsigned maxDupCostSz = 6;

    //
    // Branches between the hot and rarely run regions
    // should be minimized.  So we allow a larger size
    //
    if (rareDest != rareJump)
    {
        maxDupCostSz += 6;
    }

    if (rareDest != rareNext)
    {
        maxDupCostSz += 6;
    }

    //
    // We we are ngen-ing:
    // If the uncondional branch is a rarely run block then
    // we are willing to have more code expansion since we
    // won't be running code from this page
    //
    if (opts.jitFlags->IsSet(JitFlags::JIT_FLAG_PREJIT))
    {
        if (rareJump)
        {
            maxDupCostSz *= 2;
        }
    }

    // If the compare has too high cost then we don't want to dup

    bool costIsTooHigh = (estDupCostSz > maxDupCostSz);

#ifdef DEBUG
    if (verbose)
    {
        printf("\nDuplication of the conditional block " FMT_BB " (always branch from " FMT_BB
               ") %s, because the cost of duplication (%i) is %s than %i, validProfileWeights = %s\n",
               bDest->bbNum, bJump->bbNum, costIsTooHigh ? "not done" : "performed", estDupCostSz,
               costIsTooHigh ? "greater" : "less or equal", maxDupCostSz, allProfileWeightsAreValid ? "true" : "false");
    }
#endif // DEBUG

    if (costIsTooHigh)
    {
        return false;
    }

    /* Looks good - duplicate the conditional block */

    Statement* newStmtList = nullptr; // new stmt list to be added to bJump
    Statement* newLastStmt = nullptr;

    /* Visit all the statements in bDest */

    for (Statement* const curStmt : bDest->NonPhiStatements())
    {
        // Clone/substitute the expression.
        Statement* stmt = gtCloneStmt(curStmt);

        // cloneExpr doesn't handle everything.
        if (stmt == nullptr)
        {
            return false;
        }

        if (fgNodeThreading == NodeThreading::AllTrees)
        {
            gtSetStmtInfo(stmt);
            fgSetStmtSeq(stmt);
        }

        /* Append the expression to our list */

        if (newStmtList != nullptr)
        {
            newLastStmt->SetNextStmt(stmt);
        }
        else
        {
            newStmtList = stmt;
        }

        stmt->SetPrevStmt(newLastStmt);
        newLastStmt = stmt;
    }

    // Get to the condition node from the statement tree.
    GenTree* condTree = newLastStmt->GetRootNode();
    noway_assert(condTree->gtOper == GT_JTRUE);

    // Set condTree to the operand to the GT_JTRUE.
    condTree = condTree->AsOp()->gtOp1;

    // This condTree has to be a RelOp comparison.
    if (condTree->OperIsCompare() == false)
    {
        return false;
    }

    // Join the two linked lists.
    Statement* lastStmt = bJump->lastStmt();

    if (lastStmt != nullptr)
    {
        Statement* stmt = bJump->firstStmt();
        stmt->SetPrevStmt(newLastStmt);
        lastStmt->SetNextStmt(newStmtList);
        newStmtList->SetPrevStmt(lastStmt);
    }
    else
    {
        bJump->bbStmtList = newStmtList;
        newStmtList->SetPrevStmt(newLastStmt);
    }

    //
    // Reverse the sense of the compare
    //
    gtReverseCond(condTree);

    // We need to update the following flags of the bJump block if they were set in the bDest block
    bJump->CopyFlags(bDest, BBF_COPY_PROPAGATE);

    // Update bbRefs and bbPreds
    //
    // For now we set the likelihood of the new branch to match
    // the likelihood of the old branch.
    //
    // This may or may not match the block weight adjustments we're
    // making. All this becomes easier to reconcile once we rely on
    // edge likelihoods more and have synthesis running.
    //
    // Until then we won't worry that edges and blocks are potentially
    // out of sync.
    //
    FlowEdge* const destFalseEdge = bDest->GetFalseEdge();
    FlowEdge* const destTrueEdge  = bDest->GetTrueEdge();

    // bJump now falls through into the next block
    //
    FlowEdge* const falseEdge = fgAddRefPred(bJump->Next(), bJump, destFalseEdge);

    // bJump now jumps to bDest's normal jump target
    //
    fgRedirectTargetEdge(bJump, bDestNormalTarget);
    bJump->GetTargetEdge()->setLikelihood(destTrueEdge->getLikelihood());

    bJump->SetCond(bJump->GetTargetEdge(), falseEdge);

    if (weightJump > 0)
    {
        if (allProfileWeightsAreValid)
        {
            if (weightDest > weightJump)
            {
                bDest->bbWeight = (weightDest - weightJump);
            }
            else if (!bDest->isRunRarely())
            {
                bDest->bbWeight = BB_UNITY_WEIGHT;
            }
        }
        else
        {
            weight_t newWeightDest = 0;

            if (weightDest > weightJump)
            {
                newWeightDest = (weightDest - weightJump);
            }
            if (weightDest >= (BB_LOOP_WEIGHT_SCALE * BB_UNITY_WEIGHT) / 2)
            {
                newWeightDest = (weightDest * 2) / (BB_LOOP_WEIGHT_SCALE * BB_UNITY_WEIGHT);
            }
            if (newWeightDest > 0)
            {
                bDest->bbWeight = newWeightDest;
            }
        }
    }

#if DEBUG
    if (verbose)
    {
        // Dump out the newStmtList that we created
        printf("\nfgOptimizeBranch added these statements(s) at the end of " FMT_BB ":\n", bJump->bbNum);
        for (Statement* stmt : StatementList(newStmtList))
        {
            gtDispStmt(stmt);
        }
        printf("\nfgOptimizeBranch changed block " FMT_BB " from BBJ_ALWAYS to BBJ_COND.\n", bJump->bbNum);

        printf("\nAfter this change in fgOptimizeBranch the BB graph is:");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    return true;
}

//-----------------------------------------------------------------------------
// fgOptimizeSwitchJump: see if a switch has a dominant case, and modify to
//   check for that case up front (aka switch peeling).
//
// Returns:
//    True if the switch now has an upstream check for the dominant case.
//
bool Compiler::fgOptimizeSwitchJumps()
{
    if (!fgHasSwitch)
    {
        return false;
    }

    bool modified = false;

    for (BasicBlock* const block : Blocks())
    {
        // Lowering expands switches, so calling this method on lowered IR
        // does not make sense.
        //
        assert(!block->IsLIR());

        if (!block->KindIs(BBJ_SWITCH))
        {
            continue;
        }

        if (block->isRunRarely())
        {
            continue;
        }

        if (!block->GetSwitchTargets()->bbsHasDominantCase)
        {
            continue;
        }

        // We currently will only see dominant cases with PGO.
        //
        assert(block->hasProfileWeight());

        const unsigned dominantCase = block->GetSwitchTargets()->bbsDominantCase;

        JITDUMP(FMT_BB " has switch with dominant case %u, considering peeling\n", block->bbNum, dominantCase);

        // The dominant case should not be the default case, as we already peel that one.
        //
        assert(dominantCase < (block->GetSwitchTargets()->bbsCount - 1));
        BasicBlock* const dominantTarget = block->GetSwitchTargets()->bbsDstTab[dominantCase]->getDestinationBlock();
        Statement* const  switchStmt     = block->lastStmt();
        GenTree* const    switchTree     = switchStmt->GetRootNode();
        assert(switchTree->OperIs(GT_SWITCH));
        GenTree* const switchValue = switchTree->AsOp()->gtGetOp1();

        // Split the switch block just before at the switch.
        //
        // After this, newBlock is the switch block, and
        // block is the upstream block.
        //
        BasicBlock* newBlock = nullptr;

        if (block->firstStmt() == switchStmt)
        {
            newBlock = fgSplitBlockAtBeginning(block);
        }
        else
        {
            newBlock = fgSplitBlockAfterStatement(block, switchStmt->GetPrevStmt());
        }

        // Set up a compare in the upstream block, "stealing" the switch value tree.
        //
        GenTree* const   dominantCaseCompare = gtNewOperNode(GT_EQ, TYP_INT, switchValue, gtNewIconNode(dominantCase));
        GenTree* const   jmpTree             = gtNewOperNode(GT_JTRUE, TYP_VOID, dominantCaseCompare);
        Statement* const jmpStmt             = fgNewStmtFromTree(jmpTree, switchStmt->GetDebugInfo());
        fgInsertStmtAtEnd(block, jmpStmt);

        // Reattach switch value to the switch. This may introduce a comma
        // in the upstream compare tree, if the switch value expression is complex.
        //
        switchTree->AsOp()->gtOp1 = fgMakeMultiUse(&dominantCaseCompare->AsOp()->gtOp1);

        // Update flags
        //
        switchTree->gtFlags = switchTree->AsOp()->gtOp1->gtFlags & GTF_ALL_EFFECT;
        dominantCaseCompare->gtFlags |= dominantCaseCompare->AsOp()->gtOp1->gtFlags & GTF_ALL_EFFECT;
        jmpTree->gtFlags |= dominantCaseCompare->gtFlags & GTF_ALL_EFFECT;
        dominantCaseCompare->gtFlags |= GTF_RELOP_JMP_USED | GTF_DONT_CSE;

        // Wire up the new control flow.
        //
        FlowEdge* const blockToTargetEdge   = fgAddRefPred(dominantTarget, block);
        FlowEdge* const blockToNewBlockEdge = newBlock->bbPreds;
        block->SetCond(blockToTargetEdge, blockToNewBlockEdge);

        // Update profile data
        //
        const weight_t fraction            = newBlock->GetSwitchTargets()->bbsDominantFraction;
        const weight_t blockToTargetWeight = block->bbWeight * fraction;

        newBlock->decreaseBBProfileWeight(blockToTargetWeight);

        blockToTargetEdge->setLikelihood(fraction);
        blockToNewBlockEdge->setLikelihood(max(0.0, 1.0 - fraction));

        // For now we leave the switch as is, since there's no way
        // to indicate that one of the cases is now unreachable.
        //
        // But it no longer has a dominant case.
        //
        newBlock->GetSwitchTargets()->bbsHasDominantCase = false;

        if (fgNodeThreading == NodeThreading::AllTrees)
        {
            // The switch tree has been modified.
            JITDUMP("Rethreading " FMT_STMT "\n", switchStmt->GetID());
            gtSetStmtInfo(switchStmt);
            fgSetStmtSeq(switchStmt);

            // fgNewStmtFromTree() already threaded the tree, but calling fgMakeMultiUse() might have
            // added new nodes if a COMMA was introduced.
            JITDUMP("Rethreading " FMT_STMT "\n", jmpStmt->GetID());
            gtSetStmtInfo(jmpStmt);
            fgSetStmtSeq(jmpStmt);
        }

        modified = true;
    }

    return modified;
}

//-----------------------------------------------------------------------------
// fgExpandRunRarelyBlocks: given the current set of run rarely blocks,
//   see if we can deduce that some other blocks are run rarely.
//
// Returns:
//    True if new block was marked as run rarely.
//
bool Compiler::fgExpandRarelyRunBlocks()
{
    bool result = false;

#ifdef DEBUG
    if (verbose)
    {
        printf("\n*************** In fgExpandRarelyRunBlocks()\n");
    }

    const char* reason = nullptr;
#endif

    // Helper routine to figure out the lexically earliest predecessor
    // of bPrev that could become run rarely, given that bPrev
    // has just become run rarely.
    //
    // Note this is potentially expensive for large flow graphs and blocks
    // with lots of predecessors.
    //
    auto newRunRarely = [](BasicBlock* block, BasicBlock* bPrev) {
        // Figure out earliest block that might be impacted
        BasicBlock* bPrevPrev = nullptr;
        BasicBlock* tmpbb;

        if (bPrev->KindIs(BBJ_CALLFINALLYRET))
        {
            // If we've got a BBJ_CALLFINALLY/BBJ_CALLFINALLYRET pair, treat the BBJ_CALLFINALLY as an
            // additional predecessor for the BBJ_CALLFINALLYRET block
            tmpbb = bPrev->Prev();
            noway_assert(tmpbb->isBBCallFinallyPair());
            bPrevPrev = tmpbb;
        }

        FlowEdge* pred = bPrev->bbPreds;

        if (pred != nullptr)
        {
            // bPrevPrev will be set to the lexically
            // earliest predecessor of bPrev.

            while (pred != nullptr)
            {
                if (bPrevPrev == nullptr)
                {
                    // Initially we select the first block in the bbPreds list
                    bPrevPrev = pred->getSourceBlock();
                    continue;
                }

                // Walk the flow graph lexically forward from pred->getBlock()
                // if we find (block == bPrevPrev) then
                // pred->getBlock() is an earlier predecessor.
                for (tmpbb = pred->getSourceBlock(); tmpbb != nullptr; tmpbb = tmpbb->Next())
                {
                    if (tmpbb == bPrevPrev)
                    {
                        /* We found an earlier predecessor */
                        bPrevPrev = pred->getSourceBlock();
                        break;
                    }
                    else if (tmpbb == bPrev)
                    {
                        // We have reached bPrev so stop walking
                        // as this cannot be an earlier predecessor
                        break;
                    }
                }

                // Onto the next predecessor
                pred = pred->getNextPredEdge();
            }
        }

        if (bPrevPrev != nullptr)
        {
            // Walk the flow graph forward from bPrevPrev
            // if we don't find (tmpbb == bPrev) then our candidate
            // bPrevPrev is lexically after bPrev and we do not
            // want to select it as our new block

            for (tmpbb = bPrevPrev; tmpbb != nullptr; tmpbb = tmpbb->Next())
            {
                if (tmpbb == bPrev)
                {
                    // Set up block back to the lexically
                    // earliest predecessor of pPrev

                    return bPrevPrev;
                }
            }
        }

        // No reason to backtrack
        //
        return (BasicBlock*)nullptr;
    };

    // We expand the number of rarely run blocks by observing
    // that a block that falls into or jumps to a rarely run block,
    // must itself be rarely run and when we have a conditional
    // jump in which both branches go to rarely run blocks then
    // the block must itself be rarely run

    BasicBlock* block;
    BasicBlock* bPrev;

    for (bPrev = fgFirstBB, block = bPrev->Next(); block != nullptr; bPrev = block, block = block->Next())
    {
        if (bPrev->isRunRarely())
        {
            continue;
        }

        if (bPrev->hasProfileWeight())
        {
            continue;
        }

        INDEBUG(const char* reason = nullptr);
        bool setRarelyRun = false;

        switch (bPrev->GetKind())
        {
            case BBJ_ALWAYS:
                if (bPrev->GetTarget()->isRunRarely())
                {
                    INDEBUG(reason = "Unconditional jump to a rarely run block");
                    setRarelyRun = true;
                }
                break;

            case BBJ_CALLFINALLY:
                if (bPrev->isBBCallFinallyPair() && block->isRunRarely())
                {
                    INDEBUG(reason = "Call of finally followed rarely run continuation block");
                    setRarelyRun = true;
                }
                break;

            case BBJ_CALLFINALLYRET:
                if (bPrev->GetFinallyContinuation()->isRunRarely())
                {
                    INDEBUG(reason = "Finally continuation is a rarely run block");
                    setRarelyRun = true;
                }
                break;

            case BBJ_COND:
                if (bPrev->GetTrueTarget()->isRunRarely() && bPrev->GetFalseTarget()->isRunRarely())
                {
                    INDEBUG(reason = "Both sides of a conditional jump are rarely run");
                    setRarelyRun = true;
                }
                break;

            default:
                break;
        }

        if (setRarelyRun)
        {
            JITDUMP("%s, marking " FMT_BB " as rarely run\n", reason, bPrev->bbNum);

            // Must not have previously been marked
            noway_assert(!bPrev->isRunRarely());

            // Mark bPrev as a new rarely run block
            bPrev->bbSetRunRarely();

            // We have marked at least one block.
            //
            result = true;

            // See if we should to backtrack.
            //
            BasicBlock* bContinue = newRunRarely(block, bPrev);

            // If so, reset block to the backtrack point.
            //
            if (bContinue != nullptr)
            {
                block = bContinue;
            }
        }
    }

    // Now iterate over every block to see if we can prove that a block is rarely run
    // (i.e. when all predecessors to the block are rarely run)
    //
    for (bPrev = fgFirstBB, block = bPrev->Next(); block != nullptr; bPrev = block, block = block->Next())
    {
        // If block is not run rarely, then check to make sure that it has
        // at least one non-rarely run block.

        if (!block->isRunRarely())
        {
            bool rare = true;

            /* Make sure that block has at least one normal predecessor */
            for (BasicBlock* const predBlock : block->PredBlocks())
            {
                /* Find the fall through predecessor, if any */
                if (!predBlock->isRunRarely())
                {
                    rare = false;
                    break;
                }
            }

            if (rare)
            {
                // If 'block' is the start of a handler or filter then we cannot make it
                // rarely run because we may have an exceptional edge that
                // branches here.
                //
                if (bbIsHandlerBeg(block))
                {
                    rare = false;
                }
            }

            if (rare)
            {
                block->bbSetRunRarely();
                result = true;

#ifdef DEBUG
                if (verbose)
                {
                    printf("All branches to " FMT_BB " are from rarely run blocks, marking as rarely run\n",
                           block->bbNum);
                }
#endif // DEBUG

                // When marking a BBJ_CALLFINALLY as rarely run we also mark
                // the BBJ_CALLFINALLYRET that comes after it as rarely run
                //
                if (block->isBBCallFinallyPair())
                {
                    BasicBlock* bNext = block->Next();
                    PREFIX_ASSUME(bNext != nullptr);
                    bNext->bbSetRunRarely();
#ifdef DEBUG
                    if (verbose)
                    {
                        printf("Also marking the BBJ_CALLFINALLYRET at " FMT_BB " as rarely run\n", bNext->bbNum);
                    }
#endif // DEBUG
                }
            }
        }

        //
        // if bPrev->bbWeight is not based upon profile data we can adjust
        // the weights of bPrev and block
        //
        if (bPrev->isBBCallFinallyPair() &&         // we must have a BBJ_CALLFINALLY and BBJ_CALLFINALLYRET pair
            (bPrev->bbWeight != block->bbWeight) && // the weights are currently different
            !bPrev->hasProfileWeight())             // and the BBJ_CALLFINALLY block is not using profiled weights
        {
            if (block->isRunRarely())
            {
                // Set the BBJ_CALLFINALLY block to the same weight as the BBJ_CALLFINALLYRET block and
                // mark it rarely run.
                bPrev->bbWeight = block->bbWeight;
                bPrev->SetFlags(BBF_RUN_RARELY);
#ifdef DEBUG
                if (verbose)
                {
                    printf("Marking the BBJ_CALLFINALLY block at " FMT_BB " as rarely run because " FMT_BB
                           " is rarely run\n",
                           bPrev->bbNum, block->bbNum);
                }
#endif // DEBUG
            }
            else if (bPrev->isRunRarely())
            {
                // Set the BBJ_CALLFINALLYRET block to the same weight as the BBJ_CALLFINALLY block and
                // mark it rarely run.
                block->bbWeight = bPrev->bbWeight;
                block->SetFlags(BBF_RUN_RARELY);
#ifdef DEBUG
                if (verbose)
                {
                    printf("Marking the BBJ_CALLFINALLYRET block at " FMT_BB " as rarely run because " FMT_BB
                           " is rarely run\n",
                           block->bbNum, bPrev->bbNum);
                }
#endif // DEBUG
            }
            else // Both blocks are hot, bPrev is known not to be using profiled weight
            {
                // Set the BBJ_CALLFINALLY block to the same weight as the BBJ_CALLFINALLYRET block
                bPrev->bbWeight = block->bbWeight;
            }
            noway_assert(block->bbWeight == bPrev->bbWeight);
        }
    }

    return result;
}

#ifdef _PREFAST_
#pragma warning(push)
#pragma warning(disable : 21000) // Suppress PREFast warning about overly large function
#endif

//-----------------------------------------------------------------------------
// fgReorderBlocks: reorder blocks to favor frequent fall through paths
//   and move rare blocks to the end of the method/eh region.
//
// Arguments:
//   useProfile - if true, use profile data (if available) to more aggressively
//     reorder the blocks.
//
// Returns:
//   True if anything got reordered. Reordering blocks may require changing
//   IR to reverse branch conditions.
//
// Notes:
//   We currently allow profile-driven switch opts even when useProfile is false,
//   as they are unlikely to lead to reordering..
//
bool Compiler::fgReorderBlocks(bool useProfile)
{
    noway_assert(opts.compDbgCode == false);

    // We can't relocate anything if we only have one block
    if (fgFirstBB->IsLast())
    {
        return false;
    }

    bool newRarelyRun      = false;
    bool movedBlocks       = false;
    bool optimizedSwitches = false;
    bool optimizedBranches = false;

    // First let us expand the set of run rarely blocks
    newRarelyRun |= fgExpandRarelyRunBlocks();

#if defined(FEATURE_EH_WINDOWS_X86)
    if (!UsesFunclets())
    {
        movedBlocks |= fgRelocateEHRegions();
    }
#endif // FEATURE_EH_WINDOWS_X86

    //
    // If we are using profile weights we can change some
    // switch jumps into conditional test and jump
    //
    if (fgIsUsingProfileWeights())
    {
        optimizedSwitches = fgOptimizeSwitchJumps();
        if (optimizedSwitches)
        {
            fgUpdateFlowGraph();
        }
    }

    if (useProfile)
    {
        // Don't run the new layout until we get to the backend,
        // since LSRA can introduce new blocks, and lowering can churn the flowgraph.
        //
        if (JitConfig.JitDoReversePostOrderLayout())
        {
            return (newRarelyRun || movedBlocks || optimizedSwitches);
        }

        // We will be reordering blocks, so ensure the false target of a BBJ_COND block is its next block
        for (BasicBlock* block = fgFirstBB; block != nullptr; block = block->Next())
        {
            if (block->KindIs(BBJ_COND) && !block->NextIs(block->GetFalseTarget()))
            {
                if (block->CanRemoveJumpToTarget(block->GetTrueTarget(), this))
                {
                    // Reverse the jump condition
                    GenTree* test = block->lastNode();
                    assert(test->OperIsConditionalJump());
                    test->AsOp()->gtOp1 = gtReverseCond(test->AsOp()->gtOp1);

                    FlowEdge* const newFalseEdge = block->GetTrueEdge();
                    FlowEdge* const newTrueEdge  = block->GetFalseEdge();
                    block->SetTrueEdge(newTrueEdge);
                    block->SetFalseEdge(newFalseEdge);
                    assert(block->CanRemoveJumpToTarget(block->GetFalseTarget(), this));
                }
                else
                {
                    BasicBlock* jmpBlk = fgConnectFallThrough(block, block->GetFalseTarget());
                    assert(jmpBlk != nullptr);
                    assert(block->NextIs(jmpBlk));

                    // Skip next block
                    block = jmpBlk;
                }
            }
        }
    }

#ifdef DEBUG
    if (verbose)
    {
        printf("*************** In fgReorderBlocks()\n");

        printf("\nInitial BasicBlocks");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    BasicBlock* bNext;
    BasicBlock* bPrev;
    BasicBlock* block;
    unsigned    XTnum;
    EHblkDsc*   HBtab;

    // Iterate over every block, remembering our previous block in bPrev
    for (bPrev = fgFirstBB, block = bPrev->Next(); block != nullptr; bPrev = block, block = block->Next())
    {
        //
        // Consider relocating the rarely run blocks such that they are at the end of the method.
        // We also consider reversing conditional branches so that they become a not taken forwards branch.
        //

        // Don't consider BBJ_CALLFINALLYRET; it should be processed together with BBJ_CALLFINALLY.
        if (block->KindIs(BBJ_CALLFINALLYRET))
        {
            continue;
        }

        // If block is marked with a BBF_KEEP_BBJ_ALWAYS flag then we don't move the block
        if (block->HasFlag(BBF_KEEP_BBJ_ALWAYS))
        {
            continue;
        }

        // Finally and handlers blocks are to be kept contiguous.
        // TODO-CQ: Allow reordering within the handler region
        if (block->hasHndIndex())
        {
            continue;
        }

        bool        reorderBlock   = useProfile;
        const bool  isRare         = block->isRunRarely();
        BasicBlock* bDest          = nullptr;
        bool        forwardBranch  = false;
        bool        backwardBranch = false;

        // Setup bDest
        if (bPrev->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET))
        {
            bDest          = bPrev->GetTarget();
            forwardBranch  = fgIsForwardBranch(bPrev, bDest);
            backwardBranch = !forwardBranch;
        }
        else if (bPrev->KindIs(BBJ_COND))
        {
            // fgReorderBlocks is called in more than one optimization phase,
            // but only does any reordering in optOptimizeLayout.
            // At that point, we expect implicit fallthrough to be restored for BBJ_COND blocks.
            assert(bPrev->FalseTargetIs(block) || !reorderBlock);
            bDest          = bPrev->GetTrueTarget();
            forwardBranch  = fgIsForwardBranch(bPrev, bDest);
            backwardBranch = !forwardBranch;
        }

        // We will look for bPrev as a non rarely run block followed by block as a rarely run block
        //
        if (bPrev->isRunRarely())
        {
            reorderBlock = false;
        }

        // If the weights of the bPrev, block and bDest were all obtained from a profile run
        // then we can use them to decide if it is useful to reverse this conditional branch

        weight_t profHotWeight = -1;

        if (useProfile && bPrev->hasProfileWeight() && block->hasProfileWeight() &&
            ((bDest == nullptr) || bDest->hasProfileWeight()))
        {
            //
            // All blocks have profile information
            //
            if (forwardBranch)
            {
                if (bPrev->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET))
                {
                    if (bPrev->JumpsToNext())
                    {
                        bDest = nullptr;
                        goto CHECK_FOR_RARE;
                    }
                    // We can pull up the blocks that the unconditional jump branches to
                    // if the weight of bDest is greater or equal to the weight of block
                    // also the weight of bDest can't be zero.
                    // Don't reorder if bPrev's jump destination is the next block.
                    //
                    else if ((bDest->bbWeight < block->bbWeight) || (bDest->bbWeight == BB_ZERO_WEIGHT))
                    {
                        reorderBlock = false;
                    }
                    else
                    {
                        //
                        // If this remains true then we will try to pull up bDest to succeed bPrev
                        //
                        bool moveDestUp = true;

                        //
                        // The edge bPrev -> bDest must have a higher weight
                        // than every other edge into bDest
                        //
                        weight_t const weightToBeat = bPrev->GetTargetEdge()->getLikelyWeight();

                        // Examine all of the other edges into bDest
                        for (FlowEdge* const edge : bDest->PredEdges())
                        {
                            if (edge->getLikelyWeight() > weightToBeat)
                            {
                                moveDestUp = false;
                                break;
                            }
                        }

                        // Are we still good to move bDest up to bPrev?
                        if (moveDestUp)
                        {
                            //
                            // We will consider all blocks that have less weight than profHotWeight to be
                            // uncommonly run blocks as compared with the hot path of bPrev taken-jump to bDest
                            //
                            profHotWeight = bDest->bbWeight - 1;
                        }
                        else
                        {
                            if (block->isRunRarely())
                            {
                                // We will move any rarely run blocks blocks
                                profHotWeight = 0;
                            }
                            else
                            {
                                // We will move all blocks that have a weight less or equal to our fall through block
                                profHotWeight = block->bbWeight + 1;
                            }
                            // But we won't try to connect with bDest
                            bDest = nullptr;
                        }
                    }
                }
                else // (bPrev->KindIs(BBJ_COND))
                {
                    noway_assert(bPrev->KindIs(BBJ_COND));
                    //
                    // We will reverse branch if the true edge's likelihood is more than 51%.
                    //
                    // We will set up profHotWeight to be maximum bbWeight that a block
                    // could have for us not to want to reverse the conditional branch.
                    //
                    // We will consider all blocks that have less weight than profHotWeight to be
                    // uncommonly run blocks compared to the weight of bPrev's true edge.
                    //
                    // We will check if bPrev's true edge weight
                    // is more than twice bPrev's false edge weight.
                    //
                    //                  bPrev -->   [BB04, weight 100]
                    //                                     |         \.
                    //          falseEdge ---------------> O          \.
                    //          [likelihood=0.33]          V           \.
                    //                  block -->   [BB05, weight 33]   \.
                    //                                                   \.
                    //          trueEdge ------------------------------> O
                    //          [likelihood=0.67]                        |
                    //                                                   V
                    //                  bDest --------------->   [BB08, weight 67]
                    //
                    assert(bPrev->FalseTargetIs(block));
                    FlowEdge* trueEdge  = bPrev->GetTrueEdge();
                    FlowEdge* falseEdge = bPrev->GetFalseEdge();
                    noway_assert(trueEdge != nullptr);
                    noway_assert(falseEdge != nullptr);

                    // If we take the true branch more than half the time, we will reverse the branch.
                    if (trueEdge->getLikelihood() < 0.51)
                    {
                        reorderBlock = false;
                    }
                    else
                    {
                        // set profHotWeight
                        profHotWeight = falseEdge->getLikelyWeight() - 1;
                    }
                }
            }
            else // not a forwardBranch
            {
                if (bPrev->bbFallsThrough())
                {
                    goto CHECK_FOR_RARE;
                }

                // Here we should pull up the highest weight block remaining
                // and place it here since bPrev does not fall through.

                weight_t    highestWeight           = 0;
                BasicBlock* candidateBlock          = nullptr;
                BasicBlock* lastNonFallThroughBlock = bPrev;
                BasicBlock* bTmp                    = bPrev->Next();

                while (bTmp != nullptr)
                {
                    // Don't try to split a call finally pair
                    //
                    if (bTmp->isBBCallFinallyPair())
                    {
                        // Move bTmp forward
                        bTmp = bTmp->Next();
                    }

                    //
                    // Check for loop exit condition
                    //
                    if (bTmp == nullptr)
                    {
                        break;
                    }

                    //
                    // if its weight is the highest one we've seen and
                    //  the EH regions allow for us to place bTmp after bPrev
                    //
                    if ((bTmp->bbWeight > highestWeight) && fgEhAllowsMoveBlock(bPrev, bTmp))
                    {
                        // When we have a current candidateBlock that is a conditional (or unconditional) jump
                        // to bTmp (which is a higher weighted block) then it is better to keep our current
                        // candidateBlock and have it fall into bTmp
                        //
                        if ((candidateBlock == nullptr) || !candidateBlock->KindIs(BBJ_COND, BBJ_ALWAYS) ||
                            (candidateBlock->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET) &&
                             (!candidateBlock->TargetIs(bTmp) || candidateBlock->JumpsToNext())) ||
                            (candidateBlock->KindIs(BBJ_COND) && !candidateBlock->TrueTargetIs(bTmp)))
                        {
                            // otherwise we have a new candidateBlock
                            //
                            highestWeight  = bTmp->bbWeight;
                            candidateBlock = lastNonFallThroughBlock->Next();
                        }
                    }

                    const bool bTmpJumpsToNext = bTmp->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET) && bTmp->JumpsToNext();
                    if ((!bTmp->bbFallsThrough() && !bTmpJumpsToNext) || (bTmp->bbWeight == BB_ZERO_WEIGHT))
                    {
                        lastNonFallThroughBlock = bTmp;
                    }

                    bTmp = bTmp->Next();
                }

                // If we didn't find a suitable block then skip this
                if (highestWeight == 0)
                {
                    reorderBlock = false;
                }
                else
                {
                    noway_assert(candidateBlock != nullptr);

                    // If the candidateBlock is the same a block then skip this
                    if (candidateBlock == block)
                    {
                        reorderBlock = false;
                    }
                    else
                    {
                        // Set bDest to the block that we want to come after bPrev
                        bDest = candidateBlock;

                        // set profHotWeight
                        profHotWeight = highestWeight - 1;
                    }
                }
            }
        }
        else // we don't have good profile info (or we are falling through)
        {

        CHECK_FOR_RARE:;

            /* We only want to reorder when we have a rarely run   */
            /* block right after a normal block,                   */
            /* (bPrev is known to be a normal block at this point) */
            if (!isRare)
            {
                if (block->NextIs(bDest) && block->KindIs(BBJ_RETURN) && bPrev->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET))
                {
                    // This is a common case with expressions like "return Expr1 && Expr2" -- move the return
                    // to establish fall-through.
                }
                else
                {
                    reorderBlock = false;
                }
            }
            else
            {
                /* If the jump target bDest is also a rarely run block then we don't want to do the reversal */
                if (bDest && bDest->isRunRarely())
                {
                    reorderBlock = false; /* Both block and bDest are rarely run */
                }
                else
                {
                    // We will move any rarely run blocks blocks
                    profHotWeight = 0;
                }
            }
        }

        if (reorderBlock == false)
        {
            //
            // Check for an unconditional branch to a conditional branch
            // which also branches back to our next block
            //
            const bool optimizedBranch = fgOptimizeBranch(bPrev);
            if (optimizedBranch)
            {
                noway_assert(bPrev->KindIs(BBJ_COND));
                optimizedBranches = true;
            }
            continue;
        }

        //  Now we need to determine which blocks should be moved
        //
        //  We consider one of two choices:
        //
        //  1. Moving the fall-through blocks (or rarely run blocks) down to
        //     later in the method and hopefully connecting the jump dest block
        //     so that it becomes the fall through block
        //
        //  And when bDest is not NULL, we also consider:
        //
        //  2. Moving the bDest block (or blocks) up to bPrev
        //     so that it could be used as a fall through block
        //
        //  We will prefer option #1 if we are able to connect the jump dest
        //  block as the fall though block otherwise will we try to use option #2
        //

        //
        //  Consider option #1: relocating blocks starting at 'block'
        //    to later in flowgraph
        //
        // We set bStart to the first block that will be relocated
        // and bEnd to the last block that will be relocated

        BasicBlock* bStart   = block;
        BasicBlock* bEnd     = bStart;
        bNext                = bEnd->Next();
        bool connected_bDest = false;

        if ((backwardBranch && !isRare) || block->HasFlag(BBF_DONT_REMOVE)) // Don't choose option #1 when block is the
                                                                            // start of a try region
        {
            bStart = nullptr;
            bEnd   = nullptr;
        }
        else
        {
            while (true)
            {
                // Don't try to split a call finally pair
                //
                if (bEnd->isBBCallFinallyPair())
                {
                    // Move bEnd and bNext forward
                    bEnd  = bNext;
                    bNext = bNext->Next();
                }

                //
                // Check for loop exit condition
                //
                if (bNext == nullptr)
                {
                    break;
                }

                // Check if we've reached the funclets region, at the end of the function
                if (bEnd->NextIs(fgFirstFuncletBB))
                {
                    break;
                }

                if (bNext == bDest)
                {
                    connected_bDest = true;
                    break;
                }

                // All the blocks must have the same try index
                // and must not have the BBF_DONT_REMOVE flag set

                if (!BasicBlock::sameTryRegion(bStart, bNext) || bNext->HasFlag(BBF_DONT_REMOVE))
                {
                    // exit the loop, bEnd is now set to the
                    // last block that we want to relocate
                    break;
                }

                // If we are relocating rarely run blocks..
                if (isRare)
                {
                    // ... then all blocks must be rarely run
                    if (!bNext->isRunRarely())
                    {
                        // exit the loop, bEnd is now set to the
                        // last block that we want to relocate
                        break;
                    }
                }
                else
                {
                    // If we are moving blocks that are hot then all
                    // of the blocks moved must be less than profHotWeight */
                    if (bNext->bbWeight >= profHotWeight)
                    {
                        // exit the loop, bEnd is now set to the
                        // last block that we would relocate
                        break;
                    }
                }

                // Move bEnd and bNext forward
                bEnd  = bNext;
                bNext = bNext->Next();
            }

            // Set connected_bDest to true if moving blocks [bStart .. bEnd]
            //  connects with the jump dest of bPrev (i.e bDest) and
            // thus allows bPrev fall through instead of jump.
            if (bNext == bDest)
            {
                connected_bDest = true;
            }
        }

        //  Now consider option #2: Moving the jump dest block (or blocks)
        //    up to bPrev
        //
        // The variables bStart2, bEnd2 and bPrev2 are used for option #2
        //
        // We will setup bStart2 to the first block that will be relocated
        // and bEnd2 to the last block that will be relocated
        // and bPrev2 to be the lexical pred of bDest
        //
        // If after this calculation bStart2 is NULL we cannot use option #2,
        // otherwise bStart2, bEnd2 and bPrev2 are all non-NULL and we will use option #2

        BasicBlock* bStart2 = nullptr;
        BasicBlock* bEnd2   = nullptr;
        BasicBlock* bPrev2  = nullptr;

        // If option #1 didn't connect bDest and bDest isn't NULL
        if ((connected_bDest == false) && (bDest != nullptr) &&
            //  The jump target cannot be moved if it has the BBF_DONT_REMOVE flag set
            !bDest->HasFlag(BBF_DONT_REMOVE))
        {
            // We will consider option #2: relocating blocks starting at 'bDest' to succeed bPrev
            //
            // setup bPrev2 to be the lexical pred of bDest

            bPrev2 = block;
            while (bPrev2 != nullptr)
            {
                if (bPrev2->NextIs(bDest))
                {
                    break;
                }

                bPrev2 = bPrev2->Next();
            }

            if ((bPrev2 != nullptr) && fgEhAllowsMoveBlock(bPrev, bDest))
            {
                // We have decided that relocating bDest to be after bPrev is best
                // Set bStart2 to the first block that will be relocated
                // and bEnd2 to the last block that will be relocated
                //
                // Assigning to bStart2 selects option #2
                //
                bStart2 = bDest;
                bEnd2   = bStart2;
                bNext   = bEnd2->Next();

                while (true)
                {
                    // Don't try to split a call finally pair
                    //
                    if (bEnd2->isBBCallFinallyPair())
                    {
                        noway_assert(bNext->KindIs(BBJ_CALLFINALLYRET));
                        // Move bEnd2 and bNext forward
                        bEnd2 = bNext;
                        bNext = bNext->Next();
                    }

                    // Check for the Loop exit conditions

                    if (bNext == nullptr)
                    {
                        break;
                    }

                    if (bEnd2->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET) && bEnd2->JumpsToNext())
                    {
                        // Treat jumps to next block as fall-through
                    }
                    else if (bEnd2->bbFallsThrough() == false)
                    {
                        break;
                    }

                    // If we are relocating rarely run blocks..
                    // All the blocks must have the same try index,
                    // and must not have the BBF_DONT_REMOVE flag set

                    if (!BasicBlock::sameTryRegion(bStart2, bNext) || bNext->HasFlag(BBF_DONT_REMOVE))
                    {
                        // exit the loop, bEnd2 is now set to the
                        // last block that we want to relocate
                        break;
                    }

                    if (isRare)
                    {
                        /* ... then all blocks must not be rarely run */
                        if (bNext->isRunRarely())
                        {
                            // exit the loop, bEnd2 is now set to the
                            // last block that we want to relocate
                            break;
                        }
                    }
                    else
                    {
                        // If we are relocating hot blocks
                        // all blocks moved must be greater than profHotWeight
                        if (bNext->bbWeight <= profHotWeight)
                        {
                            // exit the loop, bEnd2 is now set to the
                            // last block that we want to relocate
                            break;
                        }
                    }

                    // Move bEnd2 and bNext forward
                    bEnd2 = bNext;
                    bNext = bNext->Next();
                }
            }
        }

        // If we are using option #1 then ...
        if (bStart2 == nullptr)
        {
            // Don't use option #1 for a backwards branch
            if (bStart == nullptr)
            {
                continue;
            }

            // .... Don't move a set of blocks that are already at the end of the main method
            if (bEnd == fgLastBBInMainFunction())
            {
                continue;
            }
        }

#ifdef DEBUG
        if (verbose)
        {
            if (bDest != nullptr)
            {
                if (bPrev->KindIs(BBJ_COND))
                {
                    printf("Decided to reverse conditional branch at block " FMT_BB " branch to " FMT_BB " ",
                           bPrev->bbNum, bDest->bbNum);
                }
                else if (bPrev->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET))
                {
                    printf("Decided to straighten unconditional branch at block " FMT_BB " branch to " FMT_BB " ",
                           bPrev->bbNum, bDest->bbNum);
                }
                else
                {
                    printf("Decided to place hot code after " FMT_BB ", placed " FMT_BB " after this block ",
                           bPrev->bbNum, bDest->bbNum);
                }

                if (profHotWeight > 0)
                {
                    printf("because of IBC profile data\n");
                }
                else
                {
                    if (bPrev->bbFallsThrough())
                    {
                        printf("since it falls into a rarely run block\n");
                    }
                    else
                    {
                        printf("since it is succeeded by a rarely run block\n");
                    }
                }
            }
            else
            {
                printf("Decided to relocate block(s) after block " FMT_BB " since they are %s block(s)\n", bPrev->bbNum,
                       block->isRunRarely() ? "rarely run" : "uncommonly run");
            }
        }
#endif // DEBUG

        // We will set insertAfterBlk to the block the precedes our insertion range
        // We will set bStartPrev to be the block that precedes the set of blocks that we are moving
        BasicBlock* insertAfterBlk;
        BasicBlock* bStartPrev;

        if (bStart2 != nullptr)
        {
            // Option #2: relocating blocks starting at 'bDest' to follow bPrev

            // Update bStart and bEnd so that we can use these two for all later operations
            bStart = bStart2;
            bEnd   = bEnd2;

            // Set bStartPrev to be the block that comes before bStart
            bStartPrev = bPrev2;

            // We will move [bStart..bEnd] to immediately after bPrev
            insertAfterBlk = bPrev;
        }
        else
        {
            // option #1: Moving the fall-through blocks (or rarely run blocks) down to later in the method

            // Set bStartPrev to be the block that come before bStart
            bStartPrev = bPrev;

            // We will move [bStart..bEnd] but we will pick the insert location later
            insertAfterBlk = nullptr;
        }

        // We are going to move [bStart..bEnd] so they can't be NULL
        noway_assert(bStart != nullptr);
        noway_assert(bEnd != nullptr);

        // bEnd can't be a BBJ_CALLFINALLY unless it is a RETLESS call
        noway_assert(!bEnd->KindIs(BBJ_CALLFINALLY) || bEnd->HasFlag(BBF_RETLESS_CALL));

        // bStartPrev must be set to the block that precedes bStart
        noway_assert(bStartPrev->NextIs(bStart));

        // Since we will be unlinking [bStart..bEnd],
        // we need to compute and remember if bStart is in each of
        // the try and handler regions
        //
        bool* fStartIsInTry = nullptr;
        bool* fStartIsInHnd = nullptr;

        if (compHndBBtabCount > 0)
        {
            fStartIsInTry = new (this, CMK_Generic) bool[compHndBBtabCount];
            fStartIsInHnd = new (this, CMK_Generic) bool[compHndBBtabCount];

            for (XTnum = 0, HBtab = compHndBBtab; XTnum < compHndBBtabCount; XTnum++, HBtab++)
            {
                fStartIsInTry[XTnum] = HBtab->InTryRegionBBRange(bStart);
                fStartIsInHnd[XTnum] = HBtab->InHndRegionBBRange(bStart);
            }
        }

        /* Temporarily unlink [bStart..bEnd] from the flow graph */
        const bool bStartPrevJumpsToNext = bStartPrev->KindIs(BBJ_ALWAYS) && bStartPrev->JumpsToNext();
        fgUnlinkRange(bStart, bEnd);

        if (insertAfterBlk == nullptr)
        {
            // Find new location for the unlinked block(s)
            // Set insertAfterBlk to the block which will precede the insertion point

            if (!bStart->hasTryIndex() && isRare)
            {
                // We'll just insert the blocks at the end of the method. If the method
                // has funclets, we will insert at the end of the main method but before
                // any of the funclets. Note that we create funclets before we call
                // fgReorderBlocks().

                insertAfterBlk = fgLastBBInMainFunction();
                noway_assert(insertAfterBlk != bPrev);
            }
            else
            {
                BasicBlock* startBlk;
                BasicBlock* lastBlk;
                EHblkDsc*   ehDsc = ehInitTryBlockRange(bStart, &startBlk, &lastBlk);

                BasicBlock* endBlk;

                /* Setup startBlk and endBlk as the range to search */

                if (ehDsc != nullptr)
                {
                    endBlk = lastBlk->Next();

                    /*
                       Multiple (nested) try regions might start from the same BB.
                       For example,

                       try3   try2   try1
                       |---   |---   |---   BB01
                       |      |      |      BB02
                       |      |      |---   BB03
                       |      |             BB04
                       |      |------------ BB05
                       |                    BB06
                       |------------------- BB07

                       Now if we want to insert in try2 region, we will start with startBlk=BB01.
                       The following loop will allow us to start from startBlk==BB04.
                    */
                    while (!BasicBlock::sameTryRegion(startBlk, bStart) && (startBlk != endBlk))
                    {
                        startBlk = startBlk->Next();
                    }

                    // startBlk cannot equal endBlk as it must come before endBlk
                    if (startBlk == endBlk)
                    {
                        goto CANNOT_MOVE;
                    }

                    // we also can't start searching the try region at bStart
                    if (startBlk == bStart)
                    {
                        // if bEnd is the last block in the method or
                        // or if bEnd->bbNext is in a different try region
                        // then we cannot move the blocks
                        //
                        if (bEnd->IsLast() || !BasicBlock::sameTryRegion(startBlk, bEnd->Next()))
                        {
                            goto CANNOT_MOVE;
                        }

                        startBlk = bEnd->Next();

                        // Check that the new startBlk still comes before endBlk

                        // startBlk cannot equal endBlk as it must come before endBlk
                        if (startBlk == endBlk)
                        {
                            goto CANNOT_MOVE;
                        }

                        BasicBlock* tmpBlk = startBlk;
                        while ((tmpBlk != endBlk) && (tmpBlk != nullptr))
                        {
                            tmpBlk = tmpBlk->Next();
                        }

                        // when tmpBlk is NULL that means startBlk is after endBlk
                        // so there is no way to move bStart..bEnd within the try region
                        if (tmpBlk == nullptr)
                        {
                            goto CANNOT_MOVE;
                        }
                    }
                }
                else
                {
                    noway_assert(isRare == false);

                    /* We'll search through the entire main method */
                    startBlk = fgFirstBB;
                    endBlk   = fgEndBBAfterMainFunction();
                }

                // Calculate nearBlk and jumpBlk and then call fgFindInsertPoint()
                // to find our insertion block
                //
                {
                    // If the set of blocks that we are moving ends with a BBJ_ALWAYS to
                    // another [rarely run] block that comes after bPrev (forward branch)
                    // then we can set up nearBlk to eliminate this jump sometimes
                    //
                    BasicBlock* nearBlk = nullptr;
                    BasicBlock* jumpBlk = nullptr;

                    if (bEnd->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET) && !bEnd->JumpsToNext() &&
                        (!isRare || bEnd->GetTarget()->isRunRarely()) &&
                        fgIsForwardBranch(bEnd, bEnd->GetTarget(), bPrev))
                    {
                        // Set nearBlk to be the block in [startBlk..endBlk]
                        // such that nearBlk->NextIs(bEnd->JumpDest)
                        // if no such block exists then set nearBlk to NULL
                        nearBlk = startBlk;
                        jumpBlk = bEnd;
                        do
                        {
                            // We do not want to set nearBlk to bPrev
                            // since then we will not move [bStart..bEnd]
                            //
                            if (nearBlk != bPrev)
                            {
                                // Check if nearBlk satisfies our requirement
                                if (nearBlk->NextIs(bEnd->GetTarget()))
                                {
                                    break;
                                }
                            }

                            // Did we reach the endBlk?
                            if (nearBlk == endBlk)
                            {
                                nearBlk = nullptr;
                                break;
                            }

                            // advance nearBlk to the next block
                            nearBlk = nearBlk->Next();

                        } while (nearBlk != nullptr);
                    }

                    // if nearBlk is NULL then we set nearBlk to be the
                    // first block that we want to insert after.
                    if (nearBlk == nullptr)
                    {
                        if (bDest != nullptr)
                        {
                            // we want to insert after bDest
                            nearBlk = bDest;
                        }
                        else
                        {
                            // we want to insert after bPrev
                            nearBlk = bPrev;
                        }
                    }

                    /* Set insertAfterBlk to the block which we will insert after. */

                    insertAfterBlk =
                        fgFindInsertPoint(bStart->bbTryIndex,
                                          true, // Insert in the try region.
                                          startBlk, endBlk, nearBlk, jumpBlk, bStart->bbWeight == BB_ZERO_WEIGHT);
                }

                /* See if insertAfterBlk is the same as where we started, */
                /*  or if we could not find any insertion point     */

                if ((insertAfterBlk == bPrev) || (insertAfterBlk == nullptr))
                {
                CANNOT_MOVE:;
                    /* We couldn't move the blocks, so put everything back */
                    /* relink [bStart .. bEnd] into the flow graph */

                    bPrev->SetNext(bStart);
                    if (!bEnd->IsLast())
                    {
                        bEnd->Next()->SetPrev(bEnd);
                    }
#ifdef DEBUG
                    if (verbose)
                    {
                        if (bStart != bEnd)
                        {
                            printf("Could not relocate blocks (" FMT_BB " .. " FMT_BB ")\n", bStart->bbNum,
                                   bEnd->bbNum);
                        }
                        else
                        {
                            printf("Could not relocate block " FMT_BB "\n", bStart->bbNum);
                        }
                    }
#endif // DEBUG
                    continue;
                }
            }
        }

        noway_assert(insertAfterBlk != nullptr);
        noway_assert(bStartPrev != nullptr);
        noway_assert(bStartPrev != insertAfterBlk);

#ifdef DEBUG
        movedBlocks = true;

        if (verbose)
        {
            const char* msg;
            if (bStart2 != nullptr)
            {
                msg = "hot";
            }
            else
            {
                if (isRare)
                {
                    msg = "rarely run";
                }
                else
                {
                    msg = "uncommon";
                }
            }

            printf("Relocated %s ", msg);
            if (bStart != bEnd)
            {
                printf("blocks (" FMT_BB " .. " FMT_BB ")", bStart->bbNum, bEnd->bbNum);
            }
            else
            {
                printf("block " FMT_BB, bStart->bbNum);
            }

            if (bPrev->KindIs(BBJ_COND))
            {
                printf(" by reversing conditional jump at " FMT_BB "\n", bPrev->bbNum);
            }
            else
            {
                printf("\n", bPrev->bbNum);
            }
        }
#endif // DEBUG

        if (bPrev->KindIs(BBJ_COND))
        {
            /* Reverse the bPrev jump condition */
            Statement* const condTestStmt = bPrev->lastStmt();
            GenTree* const   condTest     = condTestStmt->GetRootNode();

            noway_assert(condTest->gtOper == GT_JTRUE);
            condTest->AsOp()->gtOp1 = gtReverseCond(condTest->AsOp()->gtOp1);

            FlowEdge* const trueEdge  = bPrev->GetTrueEdge();
            FlowEdge* const falseEdge = bPrev->GetFalseEdge();
            bPrev->SetTrueEdge(falseEdge);
            bPrev->SetFalseEdge(trueEdge);

            // may need to rethread
            //
            if (fgNodeThreading == NodeThreading::AllTrees)
            {
                JITDUMP("Rethreading " FMT_STMT "\n", condTestStmt->GetID());
                gtSetStmtInfo(condTestStmt);
                fgSetStmtSeq(condTestStmt);
            }

            if (bStart2 != nullptr)
            {
                noway_assert(insertAfterBlk == bPrev);
                noway_assert(insertAfterBlk->NextIs(block));
            }
        }

        // If we are moving blocks that are at the end of a try or handler
        // we will need to shorten ebdTryLast or ebdHndLast
        //
        ehUpdateLastBlocks(bEnd, bStartPrev);

        // If we are moving blocks into the end of a try region or handler region
        // we will need to extend ebdTryLast or ebdHndLast so the blocks that we
        // are moving are part of this try or handler region.
        //
        for (XTnum = 0, HBtab = compHndBBtab; XTnum < compHndBBtabCount; XTnum++, HBtab++)
        {
            // Are we moving blocks to the end of a try region?
            if (HBtab->ebdTryLast == insertAfterBlk)
            {
                if (fStartIsInTry[XTnum])
                {
                    // bStart..bEnd is in the try, so extend the try region
                    fgSetTryEnd(HBtab, bEnd);
                }
            }

            // Are we moving blocks to the end of a handler region?
            if (HBtab->ebdHndLast == insertAfterBlk)
            {
                if (fStartIsInHnd[XTnum])
                {
                    // bStart..bEnd is in the handler, so extend the handler region
                    fgSetHndEnd(HBtab, bEnd);
                }
            }
        }

        /* We have decided to insert the block(s) after 'insertAfterBlk' */
        fgMoveBlocksAfter(bStart, bEnd, insertAfterBlk);

        if (bDest)
        {
            /* We may need to insert an unconditional branch after bPrev to bDest */
            fgConnectFallThrough(bPrev, bDest);
        }
        else
        {
            /* If bPrev falls through, we must insert a jump to block */
            fgConnectFallThrough(bPrev, block);
        }

        BasicBlock* bSkip = bEnd->Next();

        /* If bEnd falls through, we must insert a jump to bNext */
        fgConnectFallThrough(bEnd, bNext);

        if (bStart2 == nullptr)
        {
            /* If insertAfterBlk falls through, we are forced to     */
            /* add a jump around the block(s) we just inserted */
            fgConnectFallThrough(insertAfterBlk, bSkip);
        }
        else
        {
            /* We may need to insert an unconditional branch after bPrev2 to bStart */
            fgConnectFallThrough(bPrev2, bStart);
        }

#if DEBUG
        if (verbose)
        {
            printf("\nAfter this change in fgReorderBlocks the BB graph is:");
            fgDispBasicBlocks(verboseTrees);
            printf("\n");
        }
        fgVerifyHandlerTab();

        // Make sure that the predecessor lists are accurate
        if (expensiveDebugCheckLevel >= 2)
        {
            fgDebugCheckBBlist();
        }
#endif // DEBUG

        // Set our iteration point 'block' to be the new bPrev->bbNext
        //  It will be used as the next bPrev
        block = bPrev->Next();

    } // end of for loop(bPrev,block)

    const bool changed = movedBlocks || newRarelyRun || optimizedSwitches || optimizedBranches;

    if (changed)
    {
#if DEBUG
        // Make sure that the predecessor lists are accurate
        if (expensiveDebugCheckLevel >= 2)
        {
            fgDebugCheckBBlist();
        }
#endif // DEBUG
    }

    return changed;
}
#ifdef _PREFAST_
#pragma warning(pop)
#endif

//-----------------------------------------------------------------------------
// fgMoveHotJumps: Try to move jumps to fall into their successors, if the jump is sufficiently hot.
//
// Template parameters:
//    hasEH - If true, method has EH regions, so check that we don't try to move blocks in different regions
//
template <bool hasEH>
void Compiler::fgMoveHotJumps()
{
#ifdef DEBUG
    if (verbose)
    {
        printf("*************** In fgMoveHotJumps()\n");

        printf("\nInitial BasicBlocks");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    assert(m_dfsTree != nullptr);
    BitVecTraits traits(m_dfsTree->PostOrderTraits());
    BitVec       visitedBlocks = BitVecOps::MakeEmpty(&traits);

    // If we have a funclet region, don't bother reordering anything in it.
    //
    BasicBlock* next;
    for (BasicBlock* block = fgFirstBB; block != fgFirstFuncletBB; block = next)
    {
        next = block->Next();
        if (!m_dfsTree->Contains(block))
        {
            continue;
        }

        BitVecOps::AddElemD(&traits, visitedBlocks, block->bbPostorderNum);

        // Don't bother trying to move cold blocks
        //
        if (block->isBBWeightCold(this))
        {
            continue;
        }

        FlowEdge* targetEdge;
        FlowEdge* unlikelyEdge;

        if (block->KindIs(BBJ_ALWAYS))
        {
            targetEdge   = block->GetTargetEdge();
            unlikelyEdge = nullptr;
        }
        else if (block->KindIs(BBJ_COND))
        {
            // Consider conditional block's most likely branch for moving
            //
            if (block->GetTrueEdge()->getLikelihood() > 0.5)
            {
                targetEdge   = block->GetTrueEdge();
                unlikelyEdge = block->GetFalseEdge();
            }
            else
            {
                targetEdge   = block->GetFalseEdge();
                unlikelyEdge = block->GetTrueEdge();
            }

            // If we aren't sure which successor is hotter, and we already fall into one of them,
            // do nothing
            if ((unlikelyEdge->getLikelihood() == 0.5) && block->NextIs(unlikelyEdge->getDestinationBlock()))
            {
                continue;
            }
        }
        else
        {
            // Don't consider other block kinds
            //
            continue;
        }

        BasicBlock* target         = targetEdge->getDestinationBlock();
        bool        isBackwardJump = BitVecOps::IsMember(&traits, visitedBlocks, target->bbPostorderNum);
        assert(m_dfsTree->Contains(target));

        if (isBackwardJump)
        {
            // We don't want to change the first block, so if block is a backward jump to the first block,
            // don't try moving block before it.
            //
            if (target->IsFirst())
            {
                continue;
            }

            if (block->KindIs(BBJ_COND))
            {
                // This could be a loop exit, so don't bother moving this block up.
                // Instead, try moving the unlikely target up to create fallthrough.
                //
                targetEdge     = unlikelyEdge;
                target         = targetEdge->getDestinationBlock();
                isBackwardJump = BitVecOps::IsMember(&traits, visitedBlocks, target->bbPostorderNum);
                assert(m_dfsTree->Contains(target));

                if (isBackwardJump)
                {
                    continue;
                }
            }
            // Check for single-block loop case
            //
            else if (block == target)
            {
                continue;
            }
        }

        // Check if block already falls into target
        //
        if (block->NextIs(target))
        {
            continue;
        }

        if (target->isBBWeightCold(this))
        {
            // If target is block's most-likely successor, and block is not rarely-run,
            // perhaps the profile data is misleading, and we need to run profile repair?
            //
            continue;
        }

        if (hasEH)
        {
            // Don't move blocks in different EH regions
            //
            if (!BasicBlock::sameEHRegion(block, target))
            {
                continue;
            }

            if (isBackwardJump)
            {
                // block and target are in the same try/handler regions, and target is behind block,
                // so block cannot possibly be the start of the region.
                //
                assert(!bbIsTryBeg(block) && !bbIsHandlerBeg(block));

                // Don't change the entry block of an EH region
                //
                if (bbIsTryBeg(target) || bbIsHandlerBeg(target))
                {
                    continue;
                }
            }
            else
            {
                // block and target are in the same try/handler regions, and block is behind target,
                // so target cannot possibly be the start of the region.
                //
                assert(!bbIsTryBeg(target) && !bbIsHandlerBeg(target));
            }
        }

        // If moving block will break up existing fallthrough behavior into target, make sure it's worth it
        //
        FlowEdge* const fallthroughEdge = fgGetPredForBlock(target, target->Prev());
        if ((fallthroughEdge != nullptr) && (fallthroughEdge->getLikelyWeight() >= targetEdge->getLikelyWeight()))
        {
            continue;
        }

        if (isBackwardJump)
        {
            // Move block to before target
            //
            fgUnlinkBlock(block);
            fgInsertBBbefore(target, block);
        }
        else if (hasEH && target->isBBCallFinallyPair())
        {
            // target is a call-finally pair, so move the pair up to block
            //
            fgUnlinkRange(target, target->Next());
            fgMoveBlocksAfter(target, target->Next(), block);
            next = target->Next();
        }
        else
        {
            // Move target up to block
            //
            fgUnlinkBlock(target);
            fgInsertBBafter(block, target);
            next = target;
        }
    }
}

//-----------------------------------------------------------------------------
// fgDoReversePostOrderLayout: Reorder blocks using a greedy RPO traversal,
// taking care to keep loop bodies compact.
//
void Compiler::fgDoReversePostOrderLayout()
{
#ifdef DEBUG
    if (verbose)
    {
        printf("*************** In fgDoReversePostOrderLayout()\n");

        printf("\nInitial BasicBlocks");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    // Compute DFS of all blocks in the method, using profile data to determine the order successors are visited in.
    //
    m_dfsTree = fgComputeDfs</* useProfile */ true>();

    // If LSRA didn't create any new blocks, we can reuse its loop-aware RPO traversal,
    // which is cached in Compiler::fgBBs.
    // If the cache isn't available, we need to recompute the loop-aware RPO.
    //
    BasicBlock** rpoSequence = fgBBs;

    if (rpoSequence == nullptr)
    {
        rpoSequence                        = new (this, CMK_BasicBlock) BasicBlock*[m_dfsTree->GetPostOrderCount()];
        FlowGraphNaturalLoops* const loops = FlowGraphNaturalLoops::Find(m_dfsTree);
        unsigned                     index = 0;
        auto                         addToSequence = [rpoSequence, &index](BasicBlock* block) {
            rpoSequence[index++] = block;
        };

        fgVisitBlocksInLoopAwareRPO(m_dfsTree, loops, addToSequence);
    }

    // Fast path: We don't have any EH regions, so just reorder the blocks
    //
    if (compHndBBtabCount == 0)
    {
        for (unsigned i = 1; i < m_dfsTree->GetPostOrderCount(); i++)
        {
            BasicBlock* const block       = rpoSequence[i - 1];
            BasicBlock* const blockToMove = rpoSequence[i];

            if (!block->NextIs(blockToMove))
            {
                fgUnlinkBlock(blockToMove);
                fgInsertBBafter(block, blockToMove);
            }
        }

        fgMoveHotJumps</* hasEH */ false>();

        return;
    }

    // The RPO will break up call-finally pairs, so save them before re-ordering
    //
    struct CallFinallyPair
    {
        BasicBlock* callFinally;
        BasicBlock* callFinallyRet;

        // Constructor provided so we can call ArrayStack::Emplace
        //
        CallFinallyPair(BasicBlock* first, BasicBlock* second)
            : callFinally(first)
            , callFinallyRet(second)
        {
        }
    };

    ArrayStack<CallFinallyPair> callFinallyPairs(getAllocator());

    for (EHblkDsc* const HBtab : EHClauses(this))
    {
        if (HBtab->HasFinallyHandler())
        {
            for (BasicBlock* const pred : HBtab->ebdHndBeg->PredBlocks())
            {
                assert(pred->KindIs(BBJ_CALLFINALLY));
                if (pred->isBBCallFinallyPair())
                {
                    callFinallyPairs.Emplace(pred, pred->Next());
                }
            }
        }
    }

    // Reorder blocks
    //
    for (unsigned i = 1; i < m_dfsTree->GetPostOrderCount(); i++)
    {
        BasicBlock* const block       = rpoSequence[i - 1];
        BasicBlock* const blockToMove = rpoSequence[i];

        // Only reorder blocks within the same EH region -- we don't want to make them non-contiguous
        //
        if (BasicBlock::sameEHRegion(block, blockToMove))
        {
            // Don't reorder EH regions with filter handlers -- we want the filter to come first
            //
            if (block->hasHndIndex() && ehGetDsc(block->getHndIndex())->HasFilter())
            {
                continue;
            }

            if (!block->NextIs(blockToMove))
            {
                fgUnlinkBlock(blockToMove);
                fgInsertBBafter(block, blockToMove);
            }
        }
    }

    // Fix up call-finally pairs
    //
    for (int i = 0; i < callFinallyPairs.Height(); i++)
    {
        const CallFinallyPair& pair = callFinallyPairs.BottomRef(i);
        fgUnlinkBlock(pair.callFinallyRet);
        fgInsertBBafter(pair.callFinally, pair.callFinallyRet);
    }

    fgMoveHotJumps</* hasEH */ true>();
}

//-----------------------------------------------------------------------------
// fgMoveColdBlocks: Move rarely-run blocks to the end of their respective regions.
//
// Notes:
//    Exception handlers are assumed to be cold, so we won't move blocks within them.
//    On platforms that don't use funclets, we should use Compiler::fgRelocateEHRegions to move cold handlers.
//    Note that Compiler::fgMoveColdBlocks will break up EH regions to facilitate intermediate transformations.
//    To reestablish contiguity of EH regions, callers need to follow this with Compiler::fgRebuildEHRegions.
//
void Compiler::fgMoveColdBlocks()
{
#ifdef DEBUG
    if (verbose)
    {
        printf("*************** In fgMoveColdBlocks()\n");

        printf("\nInitial BasicBlocks");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    auto moveBlock = [this](BasicBlock* block, BasicBlock* insertionPoint) {
        assert(block != insertionPoint);
        // Don't move handler blocks.
        // Also, leave try entries behind as a breadcrumb for where to reinsert try blocks.
        if (!bbIsTryBeg(block) && !block->hasHndIndex())
        {
            if (block->isBBCallFinallyPair())
            {
                BasicBlock* const callFinallyRet = block->Next();
                if (callFinallyRet != insertionPoint)
                {
                    fgUnlinkRange(block, callFinallyRet);
                    fgMoveBlocksAfter(block, callFinallyRet, insertionPoint);
                }
            }
            else
            {
                fgUnlinkBlock(block);
                fgInsertBBafter(insertionPoint, block);
            }
        }
    };

    BasicBlock* lastMainBB = fgLastBBInMainFunction();
    if (lastMainBB->IsFirst())
    {
        return;
    }

    // Search the main method body for rarely-run blocks to move
    //
    for (BasicBlock *block = lastMainBB->Prev(), *prev; !block->IsFirst(); block = prev)
    {
        prev = block->Prev();

        // We only want to move cold blocks.
        // Also, don't move block if it is the end of a call-finally pair,
        // as we want to keep these pairs contiguous
        // (if we encounter the beginning of a pair, we'll move the whole pair).
        //
        if (!block->isBBWeightCold(this) || block->isBBCallFinallyPairTail())
        {
            continue;
        }

        moveBlock(block, lastMainBB);
    }

    // We have moved all cold main blocks before lastMainBB to after lastMainBB.
    // If lastMainBB itself is cold, move it to the end of the method to restore its relative ordering.
    // But first, we can't move just the tail of a call-finally pair,
    // so point lastMainBB to the pair's head, if necessary.
    //
    if (lastMainBB->isBBCallFinallyPairTail())
    {
        lastMainBB = lastMainBB->Prev();
    }

    BasicBlock* lastHotBB = nullptr;
    if (lastMainBB->isBBWeightCold(this))
    {
        // lastMainBB is cold, so the block behind it (if there is one) is the last hot block
        //
        lastHotBB = lastMainBB->Prev();

        // Move lastMainBB
        //
        BasicBlock* const newLastMainBB = fgLastBBInMainFunction();
        if (lastMainBB != newLastMainBB)
        {
            moveBlock(lastMainBB, newLastMainBB);
        }
    }
    else
    {
        // lastMainBB isn't cold, so it (or its call-finally pair tail) the last hot block
        //
        lastHotBB = lastMainBB->isBBCallFinallyPair() ? lastMainBB->Next() : lastMainBB;
    }

    // Save the beginning of the cold section for later.
    // If lastHotBB is null, there isn't a hot section,
    // so there's no point in differentiating between sections for layout purposes.
    //
    fgFirstColdBlock = (lastHotBB == nullptr) ? nullptr : lastHotBB->Next();
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::EdgeCmp: Comparator for the 'cutPoints' priority queue.
// If 'left' has a bigger edge weight than 'right', 3-opt will consider it first.
// Else, 3-opt will consider 'right' first.
//
// Parameters:
//   left - One of the two edges to compare
//   right - The other edge to compare
//
// Returns:
//   True if 'right' should be considered before 'left', and false otherwise
//
/* static */ bool Compiler::ThreeOptLayout::EdgeCmp(const FlowEdge* left, const FlowEdge* right)
{
    assert(left != right);
    const weight_t leftWeight  = left->getLikelyWeight();
    const weight_t rightWeight = right->getLikelyWeight();

    // Break ties by comparing the source blocks' bbIDs.
    // If both edges are out of the same source block, use the target blocks' bbIDs.
    if (leftWeight == rightWeight)
    {
        BasicBlock* const leftSrc  = left->getSourceBlock();
        BasicBlock* const rightSrc = right->getSourceBlock();
        if (leftSrc == rightSrc)
        {
            return left->getDestinationBlock()->bbID < right->getDestinationBlock()->bbID;
        }

        return leftSrc->bbID < rightSrc->bbID;
    }

    return leftWeight < rightWeight;
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::ThreeOptLayout: Constructs a ThreeOptLayout instance.
//
// Parameters:
//   comp - The Compiler instance
//
Compiler::ThreeOptLayout::ThreeOptLayout(Compiler* comp)
    : compiler(comp)
    , cutPoints(comp->getAllocator(CMK_FlowEdge), &ThreeOptLayout::EdgeCmp)
    , ordinals(new(comp, CMK_Generic) unsigned[comp->fgBBcount]{})
    , blockOrder(nullptr)
    , tempOrder(nullptr)
    , numCandidateBlocks(0)
    , currEHRegion(0)
{
}

#ifdef DEBUG
//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::GetLayoutCost: Computes the cost of the layout for the region
// bounded by 'startPos' and 'endPos'.
//
// Parameters:
//   startPos - The starting index of the region
//   endPos - The inclusive ending index of the region
//
// Returns:
//   The region's layout cost
//
weight_t Compiler::ThreeOptLayout::GetLayoutCost(unsigned startPos, unsigned endPos)
{
    assert(startPos <= endPos);
    assert(endPos < numCandidateBlocks);
    weight_t layoutCost = BB_ZERO_WEIGHT;

    for (unsigned position = startPos; position < endPos; position++)
    {
        layoutCost += GetCost(blockOrder[position], blockOrder[position + 1]);
    }

    layoutCost += blockOrder[endPos]->bbWeight;
    return layoutCost;
}
#endif // DEBUG

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::GetCost: Computes the cost of placing 'next' after 'block'.
// Layout cost is modeled as the sum of block weights, minus the weights of edges that fall through.
//
// Parameters:
//   block - The block to consider creating fallthrough from
//   next - The block to consider creating fallthrough into
//
// Returns:
//   The cost
//
weight_t Compiler::ThreeOptLayout::GetCost(BasicBlock* block, BasicBlock* next)
{
    assert(block != nullptr);
    assert(next != nullptr);

    const weight_t  maxCost         = block->bbWeight;
    const FlowEdge* fallthroughEdge = compiler->fgGetPredForBlock(next, block);

    if (fallthroughEdge != nullptr)
    {
        // The edge's weight should never exceed its source block's weight,
        // but handle negative results from rounding errors in getLikelyWeight(), just in case
        return max(0.0, maxCost - fallthroughEdge->getLikelyWeight());
    }

    return maxCost;
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::GetPartitionCostDelta: Computes the current cost of the given partitions,
// and the cost of swapping S2 and S3, returning the difference between them.
//
// Parameters:
//   s1Start - The starting position of the first partition
//   s2Start - The starting position of the second partition
//   s3Start - The starting position of the third partition
//   s3End - The ending position (inclusive) of the third partition
//   s4End - The ending position (inclusive) of the fourth partition
//
// Returns:
//   The difference in cost between the current and proposed layouts.
//   A negative delta indicates the proposed layout is an improvement.
//
weight_t Compiler::ThreeOptLayout::GetPartitionCostDelta(
    unsigned s1Start, unsigned s2Start, unsigned s3Start, unsigned s3End, unsigned s4End)
{
    BasicBlock* const s2Block     = blockOrder[s2Start];
    BasicBlock* const s2BlockPrev = blockOrder[s2Start - 1];
    BasicBlock* const s3Block     = blockOrder[s3Start];
    BasicBlock* const s3BlockPrev = blockOrder[s3Start - 1];
    BasicBlock* const lastBlock   = blockOrder[s3End];

    // Evaluate the cost of swapping S2 and S3
    weight_t currCost = GetCost(s2BlockPrev, s2Block) + GetCost(s3BlockPrev, s3Block);
    weight_t newCost  = GetCost(s2BlockPrev, s3Block) + GetCost(lastBlock, s2Block);

    // Consider flow into S4, if the partition exists
    if (s3End < s4End)
    {
        BasicBlock* const s4StartBlock = blockOrder[s3End + 1];
        currCost += GetCost(lastBlock, s4StartBlock);
        newCost += GetCost(s3BlockPrev, s4StartBlock);
    }
    else
    {
        assert(s3End == s4End);
        currCost += lastBlock->bbWeight;
        newCost += s3BlockPrev->bbWeight;
    }

    return newCost - currCost;
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::SwapPartitions: Swap the specified partitions.
// It is assumed (and asserted) that the swap is profitable.
//
// Parameters:
//   s1Start - The starting position of the first partition
//   s2Start - The starting position of the second partition
//   s3Start - The starting position of the third partition
//   s3End - The ending position (inclusive) of the third partition
//   s4End - The ending position (inclusive) of the fourth partition
//
// Notes:
//   Here is the proposed partition:
//   S1: s1Start ~ s2Start-1
//   S2: s2Start ~ s3Start-1
//   S3: s3Start ~ s3End
//   S4: remaining blocks
//
//   After the swap:
//   S1: s1Start ~ s2Start-1
//   S3: s3Start ~ s3End
//   S2: s2Start ~ s3Start-1
//   S4: remaining blocks
//
//   If 's3End' and 's4End' are the same, the fourth partition doesn't exist.
//
void Compiler::ThreeOptLayout::SwapPartitions(
    unsigned s1Start, unsigned s2Start, unsigned s3Start, unsigned s3End, unsigned s4End)
{
    INDEBUG(const weight_t currLayoutCost = GetLayoutCost(s1Start, s4End));

    // Swap the partitions
    const unsigned     s1Size      = s2Start - s1Start;
    const unsigned     s2Size      = s3Start - s2Start;
    const unsigned     s3Size      = (s3End + 1) - s3Start;
    BasicBlock** const regionStart = blockOrder + s1Start;
    BasicBlock** const tempStart   = tempOrder + s1Start;
    memcpy(tempStart, regionStart, sizeof(BasicBlock*) * s1Size);
    memcpy(tempStart + s1Size, regionStart + s1Size + s2Size, sizeof(BasicBlock*) * s3Size);
    memcpy(tempStart + s1Size + s3Size, regionStart + s1Size, sizeof(BasicBlock*) * s2Size);

    // Copy remaining blocks in S4 over
    const unsigned numBlocks     = (s4End - s1Start) + 1;
    const unsigned swappedSize   = s1Size + s2Size + s3Size;
    const unsigned remainingSize = numBlocks - swappedSize;
    assert(numBlocks >= swappedSize);
    memcpy(tempStart + swappedSize, regionStart + swappedSize, sizeof(BasicBlock*) * remainingSize);

    std::swap(blockOrder, tempOrder);

#ifdef DEBUG
    // Don't bother checking if the cost improved for exceptionally costly layouts.
    // Imprecision from summing large floating-point values can falsely trigger the below assert.
    constexpr weight_t maxLayoutCostToCheck = (weight_t)UINT32_MAX;
    if (currLayoutCost < maxLayoutCostToCheck)
    {
        // Ensure the swap improved the overall layout. Tolerate some imprecision.
        const weight_t newLayoutCost = GetLayoutCost(s1Start, s4End);
        assert((newLayoutCost < currLayoutCost) ||
               Compiler::fgProfileWeightsEqual(newLayoutCost, currLayoutCost, 0.001));
    }
#endif // DEBUG
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::ConsiderEdge: Adds 'edge' to 'cutPoints' for later consideration
// if 'edge' looks promising, and it hasn't been considered already.
// Since adding to 'cutPoints' has logarithmic time complexity and might cause a heap allocation,
// avoid adding edges that 3-opt obviously won't consider later.
//
// Parameters:
//   edge - The branch to consider creating fallthrough for
//
void Compiler::ThreeOptLayout::ConsiderEdge(FlowEdge* edge)
{
    assert(edge != nullptr);

    // Don't add an edge that we've already considered
    // (For exceptionally branchy methods, we want to avoid exploding 'cutPoints' in size)
    if (edge->visited())
    {
        return;
    }

    BasicBlock* const srcBlk = edge->getSourceBlock();
    BasicBlock* const dstBlk = edge->getDestinationBlock();

    // Any edges under consideration should be between reachable blocks
    assert(compiler->m_dfsTree->Contains(srcBlk));
    assert(compiler->m_dfsTree->Contains(dstBlk));

    // Ignore cross-region branches
    if ((srcBlk->bbTryIndex != currEHRegion) || (dstBlk->bbTryIndex != currEHRegion))
    {
        return;
    }

    // Don't waste time reordering within handler regions.
    // Note that if a finally region is sufficiently hot,
    // we should have cloned it into the main method body already.
    if (srcBlk->hasHndIndex() || dstBlk->hasHndIndex())
    {
        return;
    }

    // For backward jumps, we will consider partitioning before 'srcBlk'.
    // If 'srcBlk' is a BBJ_CALLFINALLYRET, this partition will split up a call-finally pair.
    // Thus, don't consider edges out of BBJ_CALLFINALLYRET blocks.
    if (srcBlk->KindIs(BBJ_CALLFINALLYRET))
    {
        return;
    }

    const unsigned srcPos = ordinals[srcBlk->bbPostorderNum];
    const unsigned dstPos = ordinals[dstBlk->bbPostorderNum];

    // Don't consider edges to or from outside the hot range (i.e. ordinal doesn't match 'blockOrder' position).
    if ((srcBlk != blockOrder[srcPos]) || (dstBlk != blockOrder[dstPos]))
    {
        return;
    }

    // Don't consider edges to blocks outside the hot range (i.e. ordinal number isn't set),
    // or backedges to the first block in a region; we don't want to change the entry point.
    if ((dstPos == 0) || compiler->bbIsTryBeg(dstBlk))
    {
        return;
    }

    // Don't consider backedges for single-block loops
    if (srcPos == dstPos)
    {
        return;
    }

    edge->markVisited();
    cutPoints.Push(edge);
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::AddNonFallthroughSuccs: Considers every edge out of a given block
// that doesn't fall through as a future cut point.
//
// Parameters:
//   blockPos - The index into 'blockOrder' of the source block
//
void Compiler::ThreeOptLayout::AddNonFallthroughSuccs(unsigned blockPos)
{
    assert(blockPos < numCandidateBlocks);
    BasicBlock* const block = blockOrder[blockPos];
    BasicBlock* const next  = ((blockPos + 1) >= numCandidateBlocks) ? nullptr : blockOrder[blockPos + 1];

    for (FlowEdge* const succEdge : block->SuccEdges(compiler))
    {
        if (succEdge->getDestinationBlock() != next)
        {
            ConsiderEdge(succEdge);
        }
    }
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::AddNonFallthroughPreds: Considers every edge into a given block
// that doesn't fall through as a future cut point.
//
// Parameters:
//   blockPos - The index into 'blockOrder' of the target block
//
void Compiler::ThreeOptLayout::AddNonFallthroughPreds(unsigned blockPos)
{
    assert(blockPos < numCandidateBlocks);
    BasicBlock* const block = blockOrder[blockPos];
    BasicBlock* const prev  = (blockPos == 0) ? nullptr : blockOrder[blockPos - 1];

    for (FlowEdge* const predEdge : block->PredEdges())
    {
        if (predEdge->getSourceBlock() != prev)
        {
            ConsiderEdge(predEdge);
        }
    }
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::Run: Runs 3-opt for each contiguous region of the block list
// we're interested in reordering.
// We skip reordering handler regions for now, as these are assumed to be cold.
//
void Compiler::ThreeOptLayout::Run()
{
    // Since we moved all cold blocks to the end of the method already,
    // we should have a span of hot blocks to consider reordering at the beginning of the method
    // (unless none of the blocks are cold relative to the rest of the method,
    // in which case we will reorder the whole main method body).
    BasicBlock* const finalBlock = (compiler->fgFirstColdBlock != nullptr) ? compiler->fgFirstColdBlock->Prev()
                                                                           : compiler->fgLastBBInMainFunction();

    // Reset cold section pointer, in case we decide to do hot/cold splitting later
    compiler->fgFirstColdBlock = nullptr;

    // We better have an end block for the hot section, and it better not be the start of a call-finally pair.
    assert(finalBlock != nullptr);
    assert(!finalBlock->isBBCallFinallyPair());

    // For methods with fewer than three candidate blocks, we cannot partition anything
    if (finalBlock->IsFirst() || finalBlock->Prev()->IsFirst())
    {
        JITDUMP("Not enough blocks to partition anything. Skipping 3-opt.\n");
        return;
    }

    // Get an upper bound on the number of hot blocks without walking the whole block list.
    // We will only consider blocks reachable via normal flow.
    const unsigned numBlocksUpperBound = compiler->m_dfsTree->GetPostOrderCount();
    assert(numBlocksUpperBound != 0);
    blockOrder = new (compiler, CMK_BasicBlock) BasicBlock*[numBlocksUpperBound * 2];
    tempOrder  = (blockOrder + numBlocksUpperBound);

    // Initialize the current block order.
    // Note that we default-initialized 'ordinals' with zeros.
    // Block reordering shouldn't change the method's entry point,
    // so if a block has an ordinal of zero and it's not 'fgFirstBB',
    // the block wasn't visited below, meaning it's not in the range of candidate blocks.
    for (BasicBlock* const block : compiler->Blocks(compiler->fgFirstBB, finalBlock))
    {
        if (!compiler->m_dfsTree->Contains(block))
        {
            continue;
        }

        assert(numCandidateBlocks < numBlocksUpperBound);
        blockOrder[numCandidateBlocks] = tempOrder[numCandidateBlocks] = block;

        assert(ordinals[block->bbPostorderNum] == 0);
        ordinals[block->bbPostorderNum] = numCandidateBlocks++;

        // While walking the span of blocks to reorder,
        // remember where each try region ends within this span.
        // We'll use this information to run 3-opt per region.
        EHblkDsc* const HBtab = compiler->ehGetBlockTryDsc(block);
        if (HBtab != nullptr)
        {
            HBtab->ebdTryLast = block;
        }
    }

    // Reorder try regions first
    bool modified = false;
    for (EHblkDsc* const HBtab : EHClauses(compiler))
    {
        // If multiple region indices map to the same region,
        // make sure we reorder its blocks only once
        BasicBlock* const tryBeg = HBtab->ebdTryBeg;
        if (tryBeg->getTryIndex() != currEHRegion++)
        {
            continue;
        }

        // Ignore try regions unreachable via normal flow
        if (!compiler->m_dfsTree->Contains(tryBeg))
        {
            continue;
        }

        // Only reorder try regions within the candidate span of blocks.
        if ((ordinals[tryBeg->bbPostorderNum] != 0) || tryBeg->IsFirst())
        {
            JITDUMP("Running 3-opt for try region #%d\n", (currEHRegion - 1));
            modified |= RunThreeOptPass(tryBeg, HBtab->ebdTryLast);
        }
    }

    // Finally, reorder the main method body
    currEHRegion = 0;
    JITDUMP("Running 3-opt for main method body\n");
    modified |= RunThreeOptPass(compiler->fgFirstBB, blockOrder[numCandidateBlocks - 1]);

    if (modified)
    {
        for (unsigned i = 1; i < numCandidateBlocks; i++)
        {
            BasicBlock* const block = blockOrder[i - 1];
            BasicBlock* const next  = blockOrder[i];

            // Only reorder within EH regions to maintain contiguity.
            // TODO: Allow moving blocks in different regions when 'next' is the region entry.
            // This would allow us to move entire regions up/down because of the contiguity requirement.
            if (!block->NextIs(next) && BasicBlock::sameEHRegion(block, next))
            {
                compiler->fgUnlinkBlock(next);
                compiler->fgInsertBBafter(block, next);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::RunGreedyThreeOptPass: Runs 3-opt for the given block range,
// using a greedy strategy for finding partitions to swap.
//
// Parameters:
//   startBlock - The first block of the range to reorder
//   endBlock - The last block (inclusive) of the range to reorder
//
// Returns:
//   True if we reordered anything, false otherwise
//
// Notes:
//   For methods with more than a trivial number of basic blocks,
//   iteratively trying every cut point is prohibitively expensive.
//   Instead, add the non-fallthrough successor edges of each block to a priority queue,
//   and try to create fallthrough on each edge via partition swaps, starting with the hottest edges.
//   For each swap, repopulate the priority queue with edges along the modified cut points.
//
bool Compiler::ThreeOptLayout::RunGreedyThreeOptPass(unsigned startPos, unsigned endPos)
{
    assert(cutPoints.Empty());
    assert(startPos < endPos);
    bool modified = false;

    JITDUMP("Running greedy 3-opt pass.\n");

    // Initialize cutPoints with candidate branches in this section
    for (unsigned position = startPos; position <= endPos; position++)
    {
        AddNonFallthroughSuccs(position);
    }

    // For each candidate edge, determine if it's profitable to partition after the source block
    // and before the destination block, and swap the partitions to create fallthrough.
    // If it is, do the swap, and for the blocks before/after each cut point that lost fallthrough,
    // consider adding their successors/predecessors to 'cutPoints'.
    while (!cutPoints.Empty())
    {
        FlowEdge* const candidateEdge = cutPoints.Pop();
        candidateEdge->markUnvisited();

        BasicBlock* const srcBlk = candidateEdge->getSourceBlock();
        BasicBlock* const dstBlk = candidateEdge->getDestinationBlock();
        const unsigned    srcPos = ordinals[srcBlk->bbPostorderNum];
        const unsigned    dstPos = ordinals[dstBlk->bbPostorderNum];

        // This edge better be between blocks in the current region
        assert((srcPos >= startPos) && (srcPos <= endPos));
        assert((dstPos >= startPos) && (dstPos <= endPos));

        // 'dstBlk' better not be the region's entry point
        assert(dstPos != startPos);

        // 'srcBlk' and 'dstBlk' better be distinct
        assert(srcPos != dstPos);

        // Previous moves might have inadvertently created fallthrough from 'srcBlk' to 'dstBlk',
        // so there's nothing to do this round.
        if ((srcPos + 1) == dstPos)
        {
            assert(modified);
            continue;
        }

        // Before getting any edges, make sure 'ordinals' is accurate
        assert(blockOrder[srcPos] == srcBlk);
        assert(blockOrder[dstPos] == dstBlk);

        // To determine if it's worth creating fallthrough from 'srcBlk' into 'dstBlk',
        // we first determine the current layout cost at the proposed cut points.
        // We then compare this to the layout cost with the partitions swapped.
        // If the new cost improves upon the current cost, then we can justify the swap.

        const bool isForwardJump = (srcPos < dstPos);
        unsigned   s2Start, s3Start, s3End;
        weight_t   costChange;

        if (isForwardJump)
        {
            // Here is the proposed partition:
            // S1: startPos ~ srcPos
            // S2: srcPos+1 ~ dstPos-1
            // S3: dstPos ~ endPos
            // S4: remaining blocks
            //
            // After the swap:
            // S1: startPos ~ srcPos
            // S3: dstPos ~ endPos
            // S2: srcPos+1 ~ dstPos-1
            // S4: remaining blocks
            s2Start    = srcPos + 1;
            s3Start    = dstPos;
            s3End      = endPos;
            costChange = GetPartitionCostDelta(startPos, s2Start, s3Start, s3End, endPos);
        }
        else
        {
            // For backward jumps, we will employ a greedy 4-opt approach to find the ideal cut point
            // between the destination and source blocks.
            // Here is the proposed partition:
            // S1: startPos ~ dstPos-1
            // S2: dstPos ~ s3Start-1
            // S3: s3Start ~ srcPos
            // S4: srcPos+1 ~ endPos
            //
            // After the swap:
            // S1: startPos ~ dstPos-1
            // S3: s3Start ~ srcPos
            // S2: dstPos ~ s3Start-1
            // S4: srcPos+1 ~ endPos
            s2Start    = dstPos;
            s3Start    = srcPos;
            s3End      = srcPos;
            costChange = BB_ZERO_WEIGHT;

            // The cut points before S2 and after S3 are fixed.
            // We will search for the optimal cut point before S3.
            BasicBlock* const s2Block     = blockOrder[s2Start];
            BasicBlock* const s2BlockPrev = blockOrder[s2Start - 1];
            BasicBlock* const lastBlock   = blockOrder[s3End];

            // Because the above cut points are fixed, don't waste time re-computing their costs.
            // Instead, pre-compute them here.
            const weight_t currCostBase =
                GetCost(s2BlockPrev, s2Block) +
                ((s3End < endPos) ? GetCost(lastBlock, blockOrder[s3End + 1]) : lastBlock->bbWeight);
            const weight_t newCostBase = GetCost(lastBlock, s2Block);

            // Search for the ideal start to S3
            for (unsigned position = s2Start + 1; position <= s3End; position++)
            {
                BasicBlock* const s3Block     = blockOrder[position];
                BasicBlock* const s3BlockPrev = blockOrder[position - 1];

                // Don't consider any cut points that would break up call-finally pairs
                if (s3Block->KindIs(BBJ_CALLFINALLYRET))
                {
                    continue;
                }

                // Don't consider any cut points that would move try/handler entries
                if (compiler->bbIsTryBeg(s3BlockPrev) || compiler->bbIsHandlerBeg(s3BlockPrev))
                {
                    continue;
                }

                // Compute the cost delta of this partition
                const weight_t currCost = currCostBase + GetCost(s3BlockPrev, s3Block);
                const weight_t newCost =
                    newCostBase + GetCost(s2BlockPrev, s3Block) +
                    ((s3End < endPos) ? GetCost(s3BlockPrev, blockOrder[s3End + 1]) : s3BlockPrev->bbWeight);
                const weight_t delta = newCost - currCost;

                if (delta < costChange)
                {
                    costChange = delta;
                    s3Start    = position;
                }
            }
        }

        // Continue evaluating partitions if this one isn't profitable
        if ((costChange >= BB_ZERO_WEIGHT) || Compiler::fgProfileWeightsEqual(costChange, BB_ZERO_WEIGHT, 0.001))
        {
            continue;
        }

        JITDUMP("Swapping partitions [" FMT_BB ", " FMT_BB "] and [" FMT_BB ", " FMT_BB "] (cost change = %f)\n",
                blockOrder[s2Start]->bbNum, blockOrder[s3Start - 1]->bbNum, blockOrder[s3Start]->bbNum,
                blockOrder[s3End]->bbNum, costChange);

        SwapPartitions(startPos, s2Start, s3Start, s3End, endPos);

        // Update the ordinals for the blocks we moved
        for (unsigned i = s2Start; i <= endPos; i++)
        {
            ordinals[blockOrder[i]->bbPostorderNum] = i;
        }

        // Ensure this move created fallthrough from 'srcBlk' to 'dstBlk'
        assert((ordinals[srcBlk->bbPostorderNum] + 1) == ordinals[dstBlk->bbPostorderNum]);

        // At every cut point is an opportunity to consider more candidate edges.
        // To the left of each cut point, consider successor edges that don't fall through.
        // Ditto predecessor edges to the right of each cut point.
        AddNonFallthroughSuccs(s2Start - 1);
        AddNonFallthroughPreds(s2Start);
        AddNonFallthroughSuccs(s3Start - 1);
        AddNonFallthroughPreds(s3Start);
        AddNonFallthroughSuccs(s3End);

        if (s3End < endPos)
        {
            AddNonFallthroughPreds(s3End + 1);
        }

        modified = true;
    }

    return modified;
}

//-----------------------------------------------------------------------------
// Compiler::ThreeOptLayout::RunThreeOptPass: Runs 3-opt for the given block range.
//
// Parameters:
//   startBlock - The first block of the range to reorder
//   endBlock - The last block (inclusive) of the range to reorder
//
// Returns:
//   True if we reordered anything, false otherwise
//
bool Compiler::ThreeOptLayout::RunThreeOptPass(BasicBlock* startBlock, BasicBlock* endBlock)
{
    assert(startBlock != nullptr);
    assert(endBlock != nullptr);

    const unsigned startPos  = ordinals[startBlock->bbPostorderNum];
    const unsigned endPos    = ordinals[endBlock->bbPostorderNum];
    const unsigned numBlocks = (endPos - startPos + 1);
    assert((startPos != 0) || startBlock->IsFirst());
    assert(startPos <= endPos);

    if (numBlocks < 3)
    {
        JITDUMP("Not enough blocks to partition anything. Skipping reordering.\n");
        return false;
    }

    JITDUMP("Initial layout cost: %f\n", GetLayoutCost(startPos, endPos));
    const bool modified = RunGreedyThreeOptPass(startPos, endPos);

    // Write back to 'tempOrder' so changes to this region aren't lost next time we swap 'tempOrder' and 'blockOrder'
    if (modified)
    {
        memcpy(tempOrder + startPos, blockOrder + startPos, sizeof(BasicBlock*) * numBlocks);
        JITDUMP("Final layout cost: %f\n", GetLayoutCost(startPos, endPos));
    }
    else
    {
        JITDUMP("No changes made.\n");
    }

    return modified;
}

//-----------------------------------------------------------------------------
// fgSearchImprovedLayout: Try to improve upon RPO-based layout with the 3-opt method:
//   - Identify a range of hot blocks to reorder within
//   - Partition this set into three segments: S1 - S2 - S3
//   - Evaluate cost of swapped layout: S1 - S3 - S2
//   - If the cost improves, keep this layout
//
void Compiler::fgSearchImprovedLayout()
{
#ifdef DEBUG
    if (verbose)
    {
        printf("*************** In fgSearchImprovedLayout()\n");

        printf("\nInitial BasicBlocks");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    ThreeOptLayout layoutRunner(this);
    layoutRunner.Run();
}

//-------------------------------------------------------------
// fgUpdateFlowGraphPhase: run flow graph optimization as a
//   phase, with no tail duplication
//
// Returns:
//    Suitable phase status
//
PhaseStatus Compiler::fgUpdateFlowGraphPhase()
{
    constexpr bool doTailDup   = false;
    constexpr bool isPhase     = true;
    const bool     madeChanges = fgUpdateFlowGraph(doTailDup, isPhase);

    return madeChanges ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}

//-------------------------------------------------------------
// fgUpdateFlowGraph: Removes any empty blocks, unreachable blocks, and redundant jumps.
// Most of those appear after dead store removal and folding of conditionals.
// Also, compact consecutive basic blocks.
//
// Arguments:
//    doTailDuplication - true to attempt tail duplication optimization
//    isPhase - true if being run as the only thing in a phase
//
// Returns: true if the flowgraph has been modified
//
// Notes:
//    Debuggable code and Min Optimization JIT also introduces basic blocks
//    but we do not optimize those!
//
bool Compiler::fgUpdateFlowGraph(bool doTailDuplication /* = false */, bool isPhase /* = false */)
{
#ifdef DEBUG
    if (verbose && !isPhase)
    {
        printf("\n*************** In fgUpdateFlowGraph()");
    }
#endif // DEBUG

    /* This should never be called for debuggable code */

    noway_assert(opts.OptimizationEnabled());

    // We shouldn't be churning the flowgraph after doing hot/cold splitting
    assert(fgFirstColdBlock == nullptr);

#ifdef DEBUG
    if (verbose && !isPhase)
    {
        printf("\nBefore updating the flow graph:\n");
        fgDispBasicBlocks(verboseTrees);
        printf("\n");
    }
#endif // DEBUG

    /* Walk all the basic blocks - look for unconditional jumps, empty blocks, blocks to compact, etc...
     *
     * OBSERVATION:
     *      Once a block is removed the predecessors are not accurate (assuming they were at the beginning)
     *      For now we will only use the information in bbRefs because it is easier to be updated
     */

    bool modified = false;
    bool change;
    do
    {
        change = false;

        BasicBlock* block;           // the current block
        BasicBlock* bPrev = nullptr; // the previous non-worthless block
        BasicBlock* bNext;           // the successor of the current block
        BasicBlock* bDest;           // the jump target of the current block
        BasicBlock* bFalseDest;      // the false target of the current block (if it is a BBJ_COND)

        for (block = fgFirstBB; block != nullptr; block = block->Next())
        {
            /*  Some blocks may be already marked removed by other optimizations
             *  (e.g worthless loop removal), without being explicitly removed
             *  from the list.
             */

            if (block->HasFlag(BBF_REMOVED))
            {
                if (bPrev)
                {
                    assert(!block->IsLast());
                    bPrev->SetNext(block->Next());
                }
                else
                {
                    /* WEIRD first basic block is removed - should have an assert here */
                    noway_assert(!"First basic block marked as BBF_REMOVED???");

                    fgFirstBB = block->Next();
                }
                continue;
            }

            /*  We jump to the REPEAT label if we performed a change involving the current block
             *  This is in case there are other optimizations that can show up
             *  (e.g. - compact 3 blocks in a row)
             *  If nothing happens, we then finish the iteration and move to the next block
             */

        REPEAT:;

            bNext      = block->Next();
            bDest      = nullptr;
            bFalseDest = nullptr;

            if (block->KindIs(BBJ_ALWAYS))
            {
                bDest = block->GetTarget();
                if (doTailDuplication && fgOptimizeUncondBranchToSimpleCond(block, bDest))
                {
                    assert(block->KindIs(BBJ_COND));
                    assert(bNext == block->Next());
                    change   = true;
                    modified = true;

                    if (fgFoldSimpleCondByForwardSub(block))
                    {
                        // It is likely another pred of the target now can
                        // similarly have its control flow straightened out.
                        // Try to compact it and repeat the optimization for
                        // it.
                        if (bDest->bbRefs == 1)
                        {
                            BasicBlock* otherPred = bDest->bbPreds->getSourceBlock();
                            JITDUMP("Trying to compact last pred " FMT_BB " of " FMT_BB " that we now bypass\n",
                                    otherPred->bbNum, bDest->bbNum);
                            if (fgCanCompactBlock(otherPred))
                            {
                                fgCompactBlock(otherPred);
                                fgFoldSimpleCondByForwardSub(otherPred);

                                // Since compaction removes blocks, update lexical pointers
                                bPrev = block->Prev();
                                bNext = block->Next();
                            }
                        }

                        assert(block->KindIs(BBJ_ALWAYS));
                        bDest = block->GetTarget();
                    }
                }
            }

            // Remove jumps to the following block and optimize any JUMPS to JUMPS

            if (block->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLYRET))
            {
                bDest = block->GetTarget();
                if (bDest == bNext)
                {
                    // Skip jump optimizations, and try to compact block and bNext later
                    bDest = nullptr;
                }
            }
            else if (block->KindIs(BBJ_COND))
            {
                bDest      = block->GetTrueTarget();
                bFalseDest = block->GetFalseTarget();
                if (bDest == bFalseDest)
                {
                    fgRemoveConditionalJump(block);
                    assert(block->KindIs(BBJ_ALWAYS));
                    change     = true;
                    modified   = true;
                    bFalseDest = nullptr;
                }
            }

            if (bDest != nullptr)
            {
                // Do we have a JUMP to an empty unconditional JUMP block?
                if (bDest->KindIs(BBJ_ALWAYS) && !bDest->TargetIs(bDest) && // special case for self jumps
                    bDest->isEmpty())
                {
                    // Empty blocks that jump to the next block can probably be compacted instead
                    if (!bDest->JumpsToNext() && fgOptimizeBranchToEmptyUnconditional(block, bDest))
                    {
                        change   = true;
                        modified = true;
                        goto REPEAT;
                    }
                }

                // Check for cases where reversing the branch condition may enable
                // other flow opts.
                //
                // Current block falls through to an empty bNext BBJ_ALWAYS, and
                // (a) block jump target is bNext's bbNext.
                // (b) block jump target is elsewhere but join free, and
                //      bNext's jump target has a join.
                //
                if (block->KindIs(BBJ_COND) &&   // block is a BBJ_COND block
                    (bFalseDest == bNext) &&     // false target is the next block
                    (bNext->bbRefs == 1) &&      // no other block jumps to bNext
                    bNext->KindIs(BBJ_ALWAYS) && // the next block is a BBJ_ALWAYS block
                    !bNext->JumpsToNext() &&     // and it doesn't jump to the next block (we might compact them)
                    bNext->isEmpty() &&          // and it is an empty block
                    !bNext->TargetIs(bNext))     // special case for self jumps
                {
                    assert(block->FalseTargetIs(bNext));

                    // case (a)
                    //
                    const bool isJumpAroundEmpty = bNext->NextIs(bDest);

                    // case (b)
                    //
                    // Note the asymmetric checks for refs == 1 and refs > 1 ensures that we
                    // differentiate the roles played by bDest and bNextJumpDest. We need some
                    // sense of which arrangement is preferable to avoid getting stuck in a loop
                    // reversing and re-reversing.
                    //
                    // Other tiebreaking criteria could be considered.
                    //
                    // Pragmatic constraints:
                    //
                    // * don't consider lexical predecessors, or we may confuse loop recognition
                    // * don't consider blocks of different rarities
                    //
                    BasicBlock* const bNextJumpDest    = bNext->GetTarget();
                    const bool        isJumpToJoinFree = !isJumpAroundEmpty && (bDest->bbRefs == 1) &&
                                                  (bNextJumpDest->bbRefs > 1) && (bDest->bbNum > block->bbNum) &&
                                                  (block->isRunRarely() == bDest->isRunRarely());

                    bool optimizeJump = isJumpAroundEmpty || isJumpToJoinFree;

                    // We do not optimize jumps between two different try regions.
                    // However jumping to a block that is not in any try region is OK
                    //
                    if (bDest->hasTryIndex() && !BasicBlock::sameTryRegion(block, bDest))
                    {
                        optimizeJump = false;
                    }

                    // Also consider bNext's try region
                    //
                    if (bNext->hasTryIndex() && !BasicBlock::sameTryRegion(block, bNext))
                    {
                        optimizeJump = false;
                    }

                    if (optimizeJump && isJumpToJoinFree)
                    {
                        // In the join free case, we also need to move bDest right after bNext
                        // to create same flow as in the isJumpAroundEmpty case.
                        //
                        if (!fgEhAllowsMoveBlock(bNext, bDest) || bDest->isBBCallFinallyPair())
                        {
                            optimizeJump = false;
                        }
                        else
                        {
                            // We don't expect bDest to already be right after bNext.
                            //
                            assert(!bNext->NextIs(bDest));

                            JITDUMP("\nMoving " FMT_BB " after " FMT_BB " to enable reversal\n", bDest->bbNum,
                                    bNext->bbNum);

                            // Move bDest
                            //
                            if (ehIsBlockEHLast(bDest))
                            {
                                ehUpdateLastBlocks(bDest, bDest->Prev());
                            }

                            fgUnlinkBlock(bDest);
                            fgInsertBBafter(bNext, bDest);

                            if (ehIsBlockEHLast(bNext))
                            {
                                ehUpdateLastBlocks(bNext, bDest);
                            }
                        }
                    }

                    if (optimizeJump)
                    {
                        JITDUMP("\nReversing a conditional jump around an unconditional jump (" FMT_BB " -> " FMT_BB
                                ", " FMT_BB " -> " FMT_BB ")\n",
                                block->bbNum, bDest->bbNum, bNext->bbNum, bNextJumpDest->bbNum);

                        //  Reverse the jump condition
                        //
                        GenTree* test = block->lastNode();
                        noway_assert(test->OperIsConditionalJump());

                        if (test->OperGet() == GT_JTRUE)
                        {
                            GenTree* cond = gtReverseCond(test->AsOp()->gtOp1);
                            assert(cond == test->AsOp()->gtOp1); // Ensure `gtReverseCond` did not create a new node.
                            test->AsOp()->gtOp1 = cond;
                        }
                        else
                        {
                            gtReverseCond(test);
                        }

                        // Optimize the Conditional JUMP to go to the new target
                        //
                        FlowEdge* const oldFalseEdge = block->GetFalseEdge();
                        FlowEdge* const oldTrueEdge  = block->GetTrueEdge();
                        FlowEdge* const oldNextEdge  = bNext->GetTargetEdge();

                        // bNext no longer flows to target
                        //
                        fgRemoveRefPred(oldNextEdge);

                        // Rewire flow from block
                        //
                        block->SetFalseEdge(oldTrueEdge);
                        block->SetTrueEdge(oldFalseEdge);
                        fgRedirectTrueEdge(block, bNext->GetTarget());

                        /*
                          Unlink bNext from the BasicBlock list; note that we can
                          do this even though other blocks could jump to it - the
                          reason is that elsewhere in this function we always
                          redirect jumps to jumps to jump to the final label,
                          so even if another block jumps to bNext it won't matter
                          once we're done since any such jump will be redirected
                          to the final target by the time we're done here.
                        */

                        fgUnlinkBlockForRemoval(bNext);

                        /* Mark the block as removed */
                        bNext->SetFlags(BBF_REMOVED);

                        //
                        // If we removed the end of a try region or handler region
                        // we will need to update ebdTryLast or ebdHndLast.
                        //

                        for (EHblkDsc* const HBtab : EHClauses(this))
                        {
                            if ((HBtab->ebdTryLast == bNext) || (HBtab->ebdHndLast == bNext))
                            {
                                fgSkipRmvdBlocks(HBtab);
                            }
                        }

                        // we optimized this JUMP - goto REPEAT to catch similar cases
                        change   = true;
                        modified = true;

#ifdef DEBUG
                        if (verbose)
                        {
                            printf("\nAfter reversing the jump:\n");
                            fgDispBasicBlocks(verboseTrees);
                        }
#endif // DEBUG

                        /*
                           For a rare special case we cannot jump to REPEAT
                           as jumping to REPEAT will cause us to delete 'block'
                           because it currently appears to be unreachable.  As
                           it is a self loop that only has a single bbRef (itself)
                           However since the unlinked bNext has additional bbRefs
                           (that we will later connect to 'block'), it is not really
                           unreachable.
                        */
                        if ((bNext->bbRefs > 0) && bNext->TargetIs(block) && (block->bbRefs == 1))
                        {
                            continue;
                        }

                        goto REPEAT;
                    }
                }
            }

            //
            // Update the switch jump table such that it follows jumps to jumps:
            //
            if (block->KindIs(BBJ_SWITCH))
            {
                if (fgOptimizeSwitchBranches(block))
                {
                    change   = true;
                    modified = true;
                    goto REPEAT;
                }
            }

            noway_assert(!block->HasFlag(BBF_REMOVED));

            /* COMPACT blocks if possible */

            if (fgCanCompactBlock(block))
            {
                fgCompactBlock(block);

                /* we compacted two blocks - goto REPEAT to catch similar cases */
                change   = true;
                modified = true;
                bPrev    = block->Prev();
                goto REPEAT;
            }

            // Remove unreachable or empty blocks - do not consider blocks marked BBF_DONT_REMOVE
            // These include first and last block of a TRY, exception handlers and THROW blocks.
            if (block->HasFlag(BBF_DONT_REMOVE))
            {
                bPrev = block;
                continue;
            }

            assert(!bbIsTryBeg(block));
            noway_assert(block->bbCatchTyp == BBCT_NONE);

            /* Remove unreachable blocks
             *
             * We'll look for blocks that have countOfInEdges() = 0 (blocks may become
             * unreachable due to a BBJ_ALWAYS introduced by conditional folding for example)
             */

            if (block->countOfInEdges() == 0)
            {
                /* no references -> unreachable - remove it */
                /* For now do not update the bbNum, do it at the end */

                fgRemoveBlock(block, /* unreachable */ true);

                change   = true;
                modified = true;

                /* we removed the current block - the rest of the optimizations won't have a target
                 * continue with the next one */

                continue;
            }
            else if (block->countOfInEdges() == 1)
            {
                switch (block->GetKind())
                {
                    case BBJ_COND:
                        if (block->TrueTargetIs(block) || block->FalseTargetIs(block))
                        {
                            fgRemoveBlock(block, /* unreachable */ true);

                            change   = true;
                            modified = true;

                            /* we removed the current block - the rest of the optimizations
                             * won't have a target so continue with the next block */

                            continue;
                        }
                        break;
                    case BBJ_ALWAYS:
                        if (block->TargetIs(block))
                        {
                            fgRemoveBlock(block, /* unreachable */ true);

                            change   = true;
                            modified = true;

                            /* we removed the current block - the rest of the optimizations
                             * won't have a target so continue with the next block */

                            continue;
                        }
                        break;

                    default:
                        break;
                }
            }

            noway_assert(!block->HasFlag(BBF_REMOVED));

            /* Remove EMPTY blocks */

            if (block->isEmpty())
            {
                assert(block->PrevIs(bPrev));
                if (fgOptimizeEmptyBlock(block))
                {
                    change   = true;
                    modified = true;
                }

                /* Have we removed the block? */

                if (block->HasFlag(BBF_REMOVED))
                {
                    /* block was removed - no change to bPrev */
                    continue;
                }
            }

            /* Set the predecessor of the last reachable block
             * If we removed the current block, the predecessor remains unchanged
             * otherwise, since the current block is ok, it becomes the predecessor */

            noway_assert(!block->HasFlag(BBF_REMOVED));

            bPrev = block;
        }
    } while (change);

    // OSR entry blocks will frequently have a profile imbalance as original method execution was hijacked at them.
    // Mark the profile as inconsistent if we might have propagated the OSR entry weight.
    if (modified && opts.IsOSR())
    {
        JITDUMP("fgUpdateFlowGraph: Inconsistent OSR entry weight may have been propagated. Data %s consistent.\n",
                fgPgoConsistent ? "is now" : "was already");
        fgPgoConsistent = false;
    }

#ifdef DEBUG
    if (!isPhase)
    {
        if (verbose && modified)
        {
            printf("\nAfter updating the flow graph:\n");
            fgDispBasicBlocks(verboseTrees);
            fgDispHandlerTab();
        }

        if (compRationalIRForm)
        {
            for (BasicBlock* const block : Blocks())
            {
                LIR::AsRange(block).CheckLIR(this);
            }
        }

        fgVerifyHandlerTab();
        // Make sure that the predecessor lists are accurate
        fgDebugCheckBBlist();
        fgDebugCheckUpdate();
    }
#endif // DEBUG

    return modified;
}

//-------------------------------------------------------------
// fgDfsBlocksAndRemove: Compute DFS and delete dead blocks.
//
// Returns:
//    Suitable phase status
//
PhaseStatus Compiler::fgDfsBlocksAndRemove()
{
    fgInvalidateDfsTree();
    m_dfsTree = fgComputeDfs();

    return fgRemoveBlocksOutsideDfsTree() ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}

//-------------------------------------------------------------
// fgRemoveBlocksOutsideDfsTree: Remove the blocks that are not in the current DFS tree.
//
// Returns:
//    True if any block was removed.
//
bool Compiler::fgRemoveBlocksOutsideDfsTree()
{
    if (m_dfsTree->GetPostOrderCount() == fgBBcount)
    {
        return false;
    }

#ifdef DEBUG
    if (verbose)
    {
        printf("%u/%u blocks are unreachable and will be removed:\n", fgBBcount - m_dfsTree->GetPostOrderCount(),
               fgBBcount);
        for (BasicBlock* block : Blocks())
        {
            if (!m_dfsTree->Contains(block))
            {
                printf("  " FMT_BB "\n", block->bbNum);
            }
        }
    }
#endif // DEBUG

    // The DFS we run is not precise around call-finally, so
    // `fgRemoveUnreachableBlocks` can expose newly unreachable blocks
    // that we did not uncover during the DFS. If we did remove any
    // call-finally blocks then iterate to closure. This is a very rare
    // case.
    while (true)
    {
        bool anyCallFinallyPairs = false;
        fgRemoveUnreachableBlocks([=, &anyCallFinallyPairs](BasicBlock* block) {
            if (!m_dfsTree->Contains(block))
            {
                anyCallFinallyPairs |= block->isBBCallFinallyPair();
                return true;
            }

            return false;
        });

        if (!anyCallFinallyPairs)
        {
            break;
        }

        m_dfsTree = fgComputeDfs();
    }

#ifdef DEBUG
    // Did we actually remove all the blocks we said we were going to?
    if (verbose)
    {
        if (m_dfsTree->GetPostOrderCount() != fgBBcount)
        {
            printf("%u unreachable blocks were not removed:\n", fgBBcount - m_dfsTree->GetPostOrderCount());
            for (BasicBlock* block : Blocks())
            {
                if (!m_dfsTree->Contains(block))
                {
                    printf("  " FMT_BB "\n", block->bbNum);
                }
            }
        }
    }
#endif // DEBUG

    return true;
}

//-------------------------------------------------------------
// fgGetCodeEstimate: Compute a code size estimate for the block, including all statements
// and block control flow.
//
// Arguments:
//    block - block to consider
//
// Returns:
//    Code size estimate for block
//
unsigned Compiler::fgGetCodeEstimate(BasicBlock* block)
{
    unsigned costSz = 0; // estimate of block's code size cost

    switch (block->GetKind())
    {
        case BBJ_ALWAYS:
        case BBJ_EHCATCHRET:
        case BBJ_LEAVE:
        case BBJ_COND:
            costSz = 2;
            break;
        case BBJ_CALLFINALLY:
            costSz = 5;
            break;
        case BBJ_CALLFINALLYRET:
            costSz = 0;
            break;
        case BBJ_SWITCH:
            costSz = 10;
            break;
        case BBJ_THROW:
            costSz = 1; // We place a int3 after the code for a throw block
            break;
        case BBJ_EHFINALLYRET:
        case BBJ_EHFAULTRET:
        case BBJ_EHFILTERRET:
            costSz = 1;
            break;
        case BBJ_RETURN: // return from method
            costSz = 3;
            break;
        default:
            noway_assert(!"Bad bbKind");
            break;
    }

    for (Statement* const stmt : block->NonPhiStatements())
    {
        unsigned char cost = stmt->GetCostSz();
        costSz += cost;
    }

    return costSz;
}

#ifdef FEATURE_JIT_METHOD_PERF

//------------------------------------------------------------------------
// fgMeasureIR: count and return the number of IR nodes in the function.
//
unsigned Compiler::fgMeasureIR()
{
    unsigned nodeCount = 0;

    for (BasicBlock* const block : Blocks())
    {
        if (!block->IsLIR())
        {
            for (Statement* const stmt : block->Statements())
            {
                fgWalkTreePre(
                    stmt->GetRootNodePointer(),
                    [](GenTree** slot, fgWalkData* data) -> Compiler::fgWalkResult {
                    (*reinterpret_cast<unsigned*>(data->pCallbackData))++;
                    return Compiler::WALK_CONTINUE;
                },
                    &nodeCount);
            }
        }
        else
        {
            for (GenTree* node : LIR::AsRange(block))
            {
                nodeCount++;
            }
        }
    }

    return nodeCount;
}

#endif // FEATURE_JIT_METHOD_PERF

//------------------------------------------------------------------------
// fgHeadTailMerge: merge common sequences of statements in block predecessors/successors
//
// Parameters:
//   early - Whether this is being checked with early IR invariants (where
//           we do not have valid address exposure/GTF_GLOB_REF).
//
// Returns:
//   Suitable phase status.
//
// Notes:
//   This applies tail merging and head merging. For tail merging it looks for
//   cases where all or some predecessors of a block have the same (or
//   equivalent) last statement.
//
//   If all predecessors have the same last statement, move one of them to
//   the start of the block, and delete the copies in the preds.
//   Then retry merging.
//
//   If some predecessors have the same last statement, pick one as the
//   canonical, split it if necessary, cross jump from the others to
//   the canonical, and delete the copies in the cross jump blocks.
//   Then retry merging on the canonical block.
//
//   Conversely, for head merging, we look for cases where all successors of a
//   block start with the same statement. We then try to move one of them into
//   the predecessor (which requires special handling due to the terminator
//   node) and delete the copies.
//
//   We set a mergeLimit to try and get most of the benefit while not
//   incurring too much TP overhead. It's possible to make the merging
//   more efficient and if so it might be worth revising this value.
//
PhaseStatus Compiler::fgHeadTailMerge(bool early)
{
    bool      madeChanges = false;
    int const mergeLimit  = 50;

    const bool isEnabled = JitConfig.JitEnableHeadTailMerge() > 0;
    if (!isEnabled)
    {
        JITDUMP("Head and tail merge disabled by JitEnableHeadTailMerge\n");
        return PhaseStatus::MODIFIED_NOTHING;
    }

#ifdef DEBUG
    static ConfigMethodRange JitEnableHeadTailMergeRange;
    JitEnableHeadTailMergeRange.EnsureInit(JitConfig.JitEnableHeadTailMergeRange());
    const unsigned hash = impInlineRoot()->info.compMethodHash();
    if (!JitEnableHeadTailMergeRange.Contains(hash))
    {
        JITDUMP("Tail merge disabled by JitEnableHeadTailMergeRange\n");
        return PhaseStatus::MODIFIED_NOTHING;
    }
#endif

    struct PredInfo
    {
        PredInfo(BasicBlock* block, Statement* stmt)
            : m_block(block)
            , m_stmt(stmt)
        {
        }
        BasicBlock* m_block;
        Statement*  m_stmt;
    };

    ArrayStack<PredInfo>    predInfo(getAllocator(CMK_ArrayStack));
    ArrayStack<PredInfo>    matchedPredInfo(getAllocator(CMK_ArrayStack));
    ArrayStack<BasicBlock*> retryBlocks(getAllocator(CMK_ArrayStack));

    // Try tail merging a block.
    // If return value is true, retry.
    // May also add to retryBlocks.
    //
    auto tailMergePreds = [&](BasicBlock* commSucc) -> bool {
        // Are there enough preds to make it interesting?
        //
        if (predInfo.Height() < 2)
        {
            // Not enough preds to merge
            return false;
        }

        // If there are large numbers of viable preds, forgo trying to merge.
        // While there can be large benefits, there can also be large costs.
        //
        // Note we check this rather than countOfInEdges because we don't care
        // about dups, just the number of unique pred blocks.
        //
        if (predInfo.Height() > mergeLimit)
        {
            // Too many preds to consider
            return false;
        }

        // Find a matching set of preds. Potentially O(N^2) tree comparisons.
        //
        int i = 0;
        while (i < (predInfo.Height() - 1))
        {
            matchedPredInfo.Reset();
            matchedPredInfo.Emplace(predInfo.TopRef(i));
            Statement* const  baseStmt  = predInfo.TopRef(i).m_stmt;
            BasicBlock* const baseBlock = predInfo.TopRef(i).m_block;

            for (int j = i + 1; j < predInfo.Height(); j++)
            {
                BasicBlock* const otherBlock = predInfo.TopRef(j).m_block;

                // Consider: bypass this for statements that can't cause exceptions.
                //
                if (!BasicBlock::sameEHRegion(baseBlock, otherBlock))
                {
                    continue;
                }

                Statement* const otherStmt = predInfo.TopRef(j).m_stmt;

                // Consider: compute and cache hashes to make this faster
                //
                if (GenTree::Compare(baseStmt->GetRootNode(), otherStmt->GetRootNode()))
                {
                    matchedPredInfo.Emplace(predInfo.TopRef(j));
                }
            }

            if (matchedPredInfo.Height() < 2)
            {
                // This pred didn't match any other. Check other preds for matches.
                i++;
                continue;
            }

            // We can move the identical last statements to commSucc, if it exists,
            // and all preds have matching last statements, and we're not changing EH behavior.
            //
            bool const hasCommSucc               = (commSucc != nullptr);
            bool const predsInSameEHRegionAsSucc = hasCommSucc && BasicBlock::sameEHRegion(baseBlock, commSucc);
            bool const canMergeAllPreds = hasCommSucc && (matchedPredInfo.Height() == (int)commSucc->countOfInEdges());
            bool const canMergeIntoSucc = predsInSameEHRegionAsSucc && canMergeAllPreds;

            if (canMergeIntoSucc)
            {
                JITDUMP("All %d preds of " FMT_BB " end with the same tree, moving\n", matchedPredInfo.Height(),
                        commSucc->bbNum);
                JITDUMPEXEC(gtDispStmt(matchedPredInfo.TopRef(0).m_stmt));

                for (int j = 0; j < matchedPredInfo.Height(); j++)
                {
                    PredInfo&         info      = matchedPredInfo.TopRef(j);
                    Statement* const  stmt      = info.m_stmt;
                    BasicBlock* const predBlock = info.m_block;

                    fgUnlinkStmt(predBlock, stmt);

                    // Add one of the matching stmts to block, and
                    // update its flags.
                    //
                    if (j == 0)
                    {
                        fgInsertStmtAtBeg(commSucc, stmt);
                        commSucc->CopyFlags(predBlock, BBF_COPY_PROPAGATE);
                    }

                    madeChanges = true;
                }

                // It's worth retrying tail merge on this block.
                //
                return true;
            }

            // All or a subset of preds have matching last stmt, we will cross-jump.
            // Pick one pred block as the victim -- preferably a block with just one
            // statement or one that falls through to block (or both).
            //
            if (predsInSameEHRegionAsSucc)
            {
                JITDUMP("A subset of %d preds of " FMT_BB " end with the same tree\n", matchedPredInfo.Height(),
                        commSucc->bbNum);
            }
            else if (commSucc != nullptr)
            {
                JITDUMP("%s %d preds of " FMT_BB " end with the same tree but are in a different EH region\n",
                        canMergeAllPreds ? "All" : "A subset of", matchedPredInfo.Height(), commSucc->bbNum);
            }
            else
            {
                JITDUMP("A set of %d return blocks end with the same tree\n", matchedPredInfo.Height());
            }

            JITDUMPEXEC(gtDispStmt(matchedPredInfo.TopRef(0).m_stmt));

            BasicBlock* crossJumpVictim       = nullptr;
            Statement*  crossJumpStmt         = nullptr;
            bool        haveNoSplitVictim     = false;
            bool        haveFallThroughVictim = false;

            for (int j = 0; j < matchedPredInfo.Height(); j++)
            {
                PredInfo&         info      = matchedPredInfo.TopRef(j);
                Statement* const  stmt      = info.m_stmt;
                BasicBlock* const predBlock = info.m_block;

                // Never pick the init block as the victim as that would
                // cause us to add a predecessor to it, which is invalid.
                if (predBlock == fgFirstBB)
                {
                    continue;
                }

                bool const isNoSplit     = stmt == predBlock->firstStmt();
                bool const isFallThrough = (predBlock->KindIs(BBJ_ALWAYS) && predBlock->JumpsToNext());

                // Is this block possibly better than what we have?
                //
                bool useBlock = false;

                if (crossJumpVictim == nullptr)
                {
                    // Pick an initial candidate.
                    useBlock = true;
                }
                else if (isNoSplit && isFallThrough)
                {
                    // This is the ideal choice.
                    //
                    useBlock = true;
                }
                else if (!haveNoSplitVictim && isNoSplit)
                {
                    useBlock = true;
                }
                else if (!haveNoSplitVictim && !haveFallThroughVictim && isFallThrough)
                {
                    useBlock = true;
                }

                if (useBlock)
                {
                    crossJumpVictim       = predBlock;
                    crossJumpStmt         = stmt;
                    haveNoSplitVictim     = isNoSplit;
                    haveFallThroughVictim = isFallThrough;
                }

                // If we have the perfect victim, stop looking.
                //
                if (haveNoSplitVictim && haveFallThroughVictim)
                {
                    break;
                }
            }

            BasicBlock* crossJumpTarget = crossJumpVictim;

            // If this block requires splitting, then split it.
            // Note we know that stmt has a prev stmt.
            //
            if (haveNoSplitVictim)
            {
                JITDUMP("Will cross-jump to " FMT_BB "\n", crossJumpTarget->bbNum);
            }
            else
            {
                crossJumpTarget = fgSplitBlockAfterStatement(crossJumpVictim, crossJumpStmt->GetPrevStmt());
                JITDUMP("Will cross-jump to newly split off " FMT_BB "\n", crossJumpTarget->bbNum);
            }

            assert(!crossJumpTarget->isEmpty());

            // Do the cross jumping
            //
            for (int j = 0; j < matchedPredInfo.Height(); j++)
            {
                PredInfo&         info      = matchedPredInfo.TopRef(j);
                BasicBlock* const predBlock = info.m_block;
                Statement* const  stmt      = info.m_stmt;

                if (predBlock == crossJumpVictim)
                {
                    continue;
                }

                // remove the statement
                fgUnlinkStmt(predBlock, stmt);

                // Fix up the flow.
                //
                if (commSucc != nullptr)
                {
                    assert(predBlock->KindIs(BBJ_ALWAYS));
                    fgRedirectTargetEdge(predBlock, crossJumpTarget);
                }
                else
                {
                    FlowEdge* const newEdge = fgAddRefPred(crossJumpTarget, predBlock);
                    predBlock->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);
                }

                // For tail merge we have a common successor of predBlock and
                // crossJumpTarget, so the profile update can be done locally.
                if (crossJumpTarget->hasProfileWeight())
                {
                    crossJumpTarget->increaseBBProfileWeight(predBlock->bbWeight);
                }
            }

            // We changed things
            //
            madeChanges = true;

            // We should try tail merging the cross jump target.
            //
            retryBlocks.Push(crossJumpTarget);

            // Continue trying to merge in the current block.
            // This is a bit inefficient, we could remember how
            // far we got through the pred list perhaps.
            //
            return true;
        }

        // We've looked at everything.
        //
        return false;
    };

    auto tailMerge = [&](BasicBlock* block) -> bool {
        if (block->countOfInEdges() < 2)
        {
            // Nothing to merge here
            return false;
        }

        predInfo.Reset();

        // Find the subset of preds that reach along non-critical edges
        // and populate predInfo.
        //
        for (BasicBlock* const predBlock : block->PredBlocks())
        {
            if (predBlock->GetUniqueSucc() != block)
            {
                continue;
            }

            Statement* lastStmt = predBlock->lastStmt();

            // Block might be empty.
            //
            if (lastStmt == nullptr)
            {
                continue;
            }

            // Walk back past any GT_NOPs.
            //
            Statement* const firstStmt = predBlock->firstStmt();
            while (lastStmt->GetRootNode()->OperIs(GT_NOP))
            {
                if (lastStmt == firstStmt)
                {
                    // predBlock is evidently all GT_NOP.
                    //
                    lastStmt = nullptr;
                    break;
                }

                lastStmt = lastStmt->GetPrevStmt();
            }

            // Block might be effectively empty.
            //
            if (lastStmt == nullptr)
            {
                continue;
            }

            // We don't expect to see PHIs but watch for them anyways.
            //
            assert(!lastStmt->IsPhiDefnStmt());
            predInfo.Emplace(predBlock, lastStmt);
        }

        return tailMergePreds(block);
    };

    auto iterateTailMerge = [&](BasicBlock* block) -> void {
        int numOpts = 0;

        while (tailMerge(block))
        {
            numOpts++;
        }

        if (numOpts > 0)
        {
            JITDUMP("Did %d tail merges in " FMT_BB "\n", numOpts, block->bbNum);
        }
    };

    ArrayStack<BasicBlock*> retBlocks(getAllocator(CMK_ArrayStack));

    // Visit each block
    //
    for (BasicBlock* const block : Blocks())
    {
        iterateTailMerge(block);

        if (block->KindIs(BBJ_RETURN) && !block->isEmpty() && (block != genReturnBB))
        {
            // Avoid spitting a return away from a possible tail call
            //
            if (!block->hasSingleStmt())
            {
                Statement* const lastStmt = block->lastStmt();
                Statement* const prevStmt = lastStmt->GetPrevStmt();
                GenTree* const   prevTree = prevStmt->GetRootNode();
                if (prevTree->IsCall() && prevTree->AsCall()->CanTailCall())
                {
                    continue;
                }
            }

            retBlocks.Push(block);
        }
    }

    predInfo.Reset();
    for (int i = 0; i < retBlocks.Height(); i++)
    {
        predInfo.Push(PredInfo(retBlocks.Bottom(i), retBlocks.Bottom(i)->lastStmt()));
    }

    tailMergePreds(nullptr);

    // Work through any retries
    //
    while (retryBlocks.Height() > 0)
    {
        iterateTailMerge(retryBlocks.Pop());
    }

    // Visit each block and try to merge first statements of successors.
    //
    for (BasicBlock* const block : Blocks())
    {
        madeChanges |= fgHeadMerge(block, early);
    }

    return madeChanges ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}

//------------------------------------------------------------------------
// fgTryOneHeadMerge: Try to merge the first statement of the successors of a
// specified block.
//
// Parameters:
//   block - The block whose successors are to be considered
//   early - Whether this is being checked with early IR invariants
//           (where we do not have valid address exposure/GTF_GLOB_REF).
//
// Returns:
//   True if the merge succeeded.
//
bool Compiler::fgTryOneHeadMerge(BasicBlock* block, bool early)
{
    // We currently only check for BBJ_COND, which gets the common case of
    // spill clique created stores by the importer (often produced due to
    // ternaries in C#).
    // The logic below could be generalized to BBJ_SWITCH, but this currently
    // has almost no CQ benefit but does have a TP impact.
    if (!block->KindIs(BBJ_COND) || block->TrueEdgeIs(block->GetFalseEdge()))
    {
        return false;
    }

    // Verify that both successors are reached along non-critical edges.
    auto getSuccCandidate = [=](BasicBlock* succ, Statement** firstStmt) -> bool {
        if (succ->GetUniquePred(this) != block)
        {
            return false;
        }

        if (!BasicBlock::sameEHRegion(block, succ))
        {
            return false;
        }

        *firstStmt = nullptr;
        // Walk past any GT_NOPs.
        //
        for (Statement* stmt : succ->Statements())
        {
            if (!stmt->GetRootNode()->OperIs(GT_NOP))
            {
                *firstStmt = stmt;
                break;
            }
        }

        // Block might be effectively empty.
        //
        if (*firstStmt == nullptr)
        {
            return false;
        }

        // Cannot move terminator statement.
        //
        if ((*firstStmt == succ->lastStmt()) && succ->HasTerminator())
        {
            return false;
        }

        return true;
    };

    Statement* nextFirstStmt;
    Statement* destFirstStmt;

    if (!getSuccCandidate(block->GetFalseTarget(), &nextFirstStmt) ||
        !getSuccCandidate(block->GetTrueTarget(), &destFirstStmt))
    {
        return false;
    }

    if (!GenTree::Compare(nextFirstStmt->GetRootNode(), destFirstStmt->GetRootNode()))
    {
        return false;
    }

    JITDUMP("Both succs of " FMT_BB " start with the same tree\n", block->bbNum);
    DISPSTMT(nextFirstStmt);

    if (gtTreeContainsTailCall(nextFirstStmt->GetRootNode()) || gtTreeContainsTailCall(destFirstStmt->GetRootNode()))
    {
        JITDUMP("But one is a tailcall\n");
        return false;
    }

    JITDUMP("Checking if we can move it into the predecessor...\n");

    if (!fgCanMoveFirstStatementIntoPred(early, nextFirstStmt, block))
    {
        return false;
    }

    JITDUMP("We can; moving statement\n");

    fgUnlinkStmt(block->GetFalseTarget(), nextFirstStmt);
    fgInsertStmtNearEnd(block, nextFirstStmt);
    fgUnlinkStmt(block->GetTrueTarget(), destFirstStmt);
    block->CopyFlags(block->GetFalseTarget(), BBF_COPY_PROPAGATE);

    return true;
}

//------------------------------------------------------------------------
// fgHeadMerge: Try to repeatedly merge the first statement of the successors
// of the specified block.
//
// Parameters:
//   block               - The block whose successors are to be considered
//   early               - Whether this is being checked with early IR invariants
//                         (where we do not have valid address exposure/GTF_GLOB_REF).
//
// Returns:
//   True if any merge succeeded.
//
bool Compiler::fgHeadMerge(BasicBlock* block, bool early)
{
    bool madeChanges = false;
    int  numOpts     = 0;
    while (fgTryOneHeadMerge(block, early))
    {
        madeChanges = true;
        numOpts++;
    }

    if (numOpts > 0)
    {
        JITDUMP("Did %d head merges in " FMT_BB "\n", numOpts, block->bbNum);
    }

    return madeChanges;
}

//------------------------------------------------------------------------
// gtTreeContainsTailCall: Check if a tree contains any tail call or tail call
// candidate.
//
// Parameters:
//   tree - The tree
//
// Remarks:
//   While tail calls are generally expected to be top level nodes we do allow
//   some other shapes of calls to be tail calls, including some cascading
//   trivial assignments and casts. This function does a tree walk to check if
//   any sub tree is a tail call.
//
bool Compiler::gtTreeContainsTailCall(GenTree* tree)
{
    struct HasTailCallCandidateVisitor : GenTreeVisitor<HasTailCallCandidateVisitor>
    {
        enum
        {
            DoPreOrder = true
        };

        HasTailCallCandidateVisitor(Compiler* comp)
            : GenTreeVisitor(comp)
        {
        }

        fgWalkResult PreOrderVisit(GenTree** use, GenTree* user)
        {
            GenTree* node = *use;
            if ((node->gtFlags & GTF_CALL) == 0)
            {
                return WALK_SKIP_SUBTREES;
            }

            if (node->IsCall() && (node->AsCall()->CanTailCall() || node->AsCall()->IsTailCall()))
            {
                return WALK_ABORT;
            }

            return WALK_CONTINUE;
        }
    };

    HasTailCallCandidateVisitor visitor(this);
    return visitor.WalkTree(&tree, nullptr) == WALK_ABORT;
}

//------------------------------------------------------------------------
// fgCanMoveFirstStatementIntoPred: Check if the first statement of a block can
// be moved into its predecessor.
//
// Parameters:
//   early     - Whether this is being checked with early IR invariants (where
//               we do not have valid address exposure/GTF_GLOB_REF).
//   firstStmt - The statement to move
//   pred      - The predecessor block
//
// Remarks:
//   Unlike tail merging, for head merging we have to either spill the
//   predecessor's terminator node, or reorder it with the head statement.
//   Here we choose to reorder.
//
bool Compiler::fgCanMoveFirstStatementIntoPred(bool early, Statement* firstStmt, BasicBlock* pred)
{
    if (!pred->HasTerminator())
    {
        return true;
    }

    GenTree* tree1 = pred->lastStmt()->GetRootNode();
    GenTree* tree2 = firstStmt->GetRootNode();

    GenTreeFlags tree1Flags = tree1->gtFlags;
    GenTreeFlags tree2Flags = tree2->gtFlags;

    if (early)
    {
        tree1Flags |= gtHasLocalsWithAddrOp(tree1) ? GTF_GLOB_REF : GTF_EMPTY;
        tree2Flags |= gtHasLocalsWithAddrOp(tree2) ? GTF_GLOB_REF : GTF_EMPTY;
    }

    // We do not support embedded statements in the terminator node.
    if ((tree1Flags & GTF_ASG) != 0)
    {
        JITDUMP("  no; terminator contains embedded store\n");
        return false;
    }
    if ((tree2Flags & GTF_ASG) != 0)
    {
        // Handle common case where the second statement is a top-level store.
        if (!tree2->OperIsLocalStore())
        {
            JITDUMP("  cannot reorder with GTF_ASG without top-level store");
            return false;
        }

        GenTreeLclVarCommon* lcl = tree2->AsLclVarCommon();
        if ((lcl->Data()->gtFlags & GTF_ASG) != 0)
        {
            JITDUMP("  cannot reorder with embedded store");
            return false;
        }

        LclVarDsc* dsc = lvaGetDesc(tree2->AsLclVarCommon());
        if ((tree1Flags & GTF_ALL_EFFECT) != 0)
        {
            if (early ? dsc->lvHasLdAddrOp : dsc->IsAddressExposed())
            {
                JITDUMP("  cannot reorder store to exposed local with any side effect\n");
                return false;
            }

            if (((tree1Flags & (GTF_CALL | GTF_EXCEPT)) != 0) && pred->HasPotentialEHSuccs(this))
            {
                JITDUMP("  cannot reorder store with exception throwing tree and potential EH successor\n");
                return false;
            }
        }

        if (gtHasRef(tree1, lcl->GetLclNum()))
        {
            JITDUMP("  cannot reorder with interfering use\n");
            return false;
        }

        if (dsc->lvIsStructField && gtHasRef(tree1, dsc->lvParentLcl))
        {
            JITDUMP("  cannot reorder with interfering use of parent struct local\n");
            return false;
        }

        if (dsc->lvPromoted)
        {
            for (int i = 0; i < dsc->lvFieldCnt; i++)
            {
                if (gtHasRef(tree1, dsc->lvFieldLclStart + i))
                {
                    JITDUMP("  cannot reorder with interfering use of struct field\n");
                    return false;
                }
            }
        }

        // We've validated that the store does not interfere. Get rid of the
        // flag for the future checks.
        tree2Flags &= ~GTF_ASG;
    }

    if (((tree1Flags & GTF_CALL) != 0) && ((tree2Flags & GTF_ALL_EFFECT) != 0))
    {
        JITDUMP("  cannot reorder call with any side effect\n");
        return false;
    }
    if (((tree1Flags & GTF_GLOB_REF) != 0) && ((tree2Flags & GTF_PERSISTENT_SIDE_EFFECTS) != 0))
    {
        JITDUMP("  cannot reorder global reference with persistent side effects\n");
        return false;
    }
    if ((tree1Flags & GTF_ORDER_SIDEEFF) != 0)
    {
        if ((tree2Flags & (GTF_GLOB_REF | GTF_ORDER_SIDEEFF)) != 0)
        {
            JITDUMP("  cannot reorder ordering side effect\n");
            return false;
        }
    }
    if ((tree2Flags & GTF_ORDER_SIDEEFF) != 0)
    {
        if ((tree1Flags & (GTF_GLOB_REF | GTF_ORDER_SIDEEFF)) != 0)
        {
            JITDUMP("  cannot reorder ordering side effect\n");
            return false;
        }
    }
    if (((tree1Flags & GTF_EXCEPT) != 0) && ((tree2Flags & GTF_SIDE_EFFECT) != 0))
    {
        JITDUMP("  cannot reorder exception with side effect\n");
        return false;
    }

    return true;
}
