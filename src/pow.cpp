// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include <stdlib.h>     /* abs */
#include "util.h"

extern ArgsManager gArgs;

const int64_t GetDeltaTime(const int64_t nDeltaHeight, const CBlockIndex* pindexLast)
{
    int64_t nHeightFirst = pindexLast->nHeight - nDeltaHeight;
    while (1)
    {
        assert(nHeightFirst > 0);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        assert(pindexFirst);    
        
        const int64_t ret = abs(pindexLast->GetBlockTime() - pindexFirst->GetBlockTime());
        LogPrintf("nHeightLast = %i; nHeightFirst = %i; timeLast=%i; timeFirst=%i\n", pindexLast->nHeight, pindexFirst->nHeight, pindexLast->GetBlockTime(), pindexFirst->GetBlockTime());
        
        if (ret == 0)
        {
            nHeightFirst--;
            continue;
        }
        
        return ret;
    }
    return 1000*600;
}

unsigned int GetNextWorkRequiredMC2(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    int64_t nAverageBlockTime = 0;
    arith_uint256 bnAverageBits = 0;

    const CBlockIndex* pindexCurr = pindexLast;
    for (int n=0; n<6; n++)
    {
        arith_uint256 bnTmp;
        bnTmp.SetCompact(pindexCurr->nBits);
        
        const int64_t nDeltaTime = abs(pindexCurr->GetBlockTime() - pindexCurr->pprev->GetBlockTime());

        nAverageBlockTime += nDeltaTime;
        bnAverageBits += bnTmp;
        
        pindexCurr = pindexCurr->pprev;
    }

    nAverageBlockTime = (nAverageBlockTime) / 6;
    bnAverageBits = (bnAverageBits) / 6;

    const int64_t nDeltaTimeBlocks6 = GetDeltaTime(6, pindexLast);
    const int64_t nDeltaTimeBlocks144 = GetDeltaTime(144, pindexLast);
    
    LogPrintf("nAverageBlockTime=%i; nDeltaTimeBlocks6=%i; nDeltaTimeBlocks144=%i\n", nAverageBlockTime, nDeltaTimeBlocks6, nDeltaTimeBlocks144);
    if (nAverageBlockTime < 300) 
        bnAverageBits = (bnAverageBits * (nDeltaTimeBlocks6 + 5*600*6)) / (6*600*6) ;
    else
    {
        if (nAverageBlockTime < 600) 
            bnAverageBits = (bnAverageBits * (nDeltaTimeBlocks144 + 49*600*144)) / (50 * 600 * 144);
    }
    
    if (nAverageBlockTime > 900) 
         bnAverageBits = (bnAverageBits * nDeltaTimeBlocks6) / (600*6);
    else
    {
        if (nAverageBlockTime > 630) 
            bnAverageBits = (bnAverageBits * (nDeltaTimeBlocks6 + 5*600*6)) / (6*600*6) ;
    }
    
    if (nAverageBlockTime >= 600 && nAverageBlockTime <= 630 )
        return pindexLast->nBits;

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    if (bnAverageBits > bnPowLimit)
        bnAverageBits = bnPowLimit;

    return bnAverageBits.GetCompact();
}


unsigned int CalculateNextWorkRequiredMC(const CBlockIndex* pindexBase, const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexBase->nBits;
        
    int64_t nPowTargetTimespan = params.nPowTargetTimespan*3;
    
    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < nPowTargetTimespan/4)
        nActualTimespan = nPowTargetTimespan/4;
    if (nActualTimespan > nPowTargetTimespan*4)
        nActualTimespan = nPowTargetTimespan*4;
    
    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexBase->nBits);
    
    if (pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime() < (7*params.nPowTargetSpacing)/10 && nActualTimespan > nPowTargetTimespan)
        return bnNew.GetCompact();

    bnNew *= nActualTimespan;
    bnNew /= nPowTargetTimespan;
    
    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;
        
    return bnNew.GetCompact();
} 

unsigned int GetNextWorkRequiredMC(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    
    if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*4)
        return nProofOfWorkLimit;
    
    int nHeightFirst = pindexLast->nHeight - 18;
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);
    
    const CBlockIndex* pindexBase = pindexLast;
    while (pindexBase->pprev && pindexBase->nBits == nProofOfWorkLimit)
        pindexBase = pindexBase->pprev;
    
    if (pindexBase->GetBlockTime() < pindexFirst->GetBlockTime())
        pindexBase = pindexLast;
    
    return CalculateNextWorkRequiredMC(pindexBase, pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    
    int nFork = gArgs.GetArg("-fork", 88000);

    if (pindexLast->nHeight >= nFork)
        return GetNextWorkRequiredMC2(pindexLast, pblock, params);
    if (pindexLast->nHeight >= 26000)
        return GetNextWorkRequiredMC(pindexLast, pblock, params);

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0 || pindexLast->nHeight == 16200)
    {
        if (params.fPowAllowMinDifficultyBlocks || pindexLast->nHeight >= 16200)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}