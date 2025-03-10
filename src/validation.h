// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_H
#define BITCOIN_VALIDATION_H

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <arith_uint256.h>
#include <attributes.h>
#include <blockfileinfo.h>
#include <blockindexworkcomparator.h>
#include <chain.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <disconnectresult.h>
#include <flatfile.h>
#include <fs.h>
#include <node/blockstorage.h>
#include <policy/packages.h>
#include <script/script_error.h>
#include <script/script_metrics.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h> // For CTxMemPool::cs
#include <uint256.h>
#include <util/check.h>
#include <util/translation.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class CChainParams;
class CChainState;
class ChainstateManager;
class Config;
class CScriptCheck;
class CTxMemPool;
class CTxUndo;
class DisconnectedBlockTransactions;

struct ChainTxData;
struct FlatFilePos;
struct PrecomputedTransactionData;
struct LockPoints;
struct AssumeutxoData;
namespace node {
class SnapshotMetadata;
} // namespace node

namespace Consensus {
struct Params;
}

#define MIN_TRANSACTION_SIZE                                                   \
    (::GetSerializeSize(CTransaction(), PROTOCOL_VERSION))

/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const Amount DEFAULT_MIN_RELAY_TX_FEE_PER_KB(1000 * SATOSHI);
/** Default for -excessutxocharge for transactions transactions */
static const Amount DEFAULT_UTXO_FEE = Amount::zero();
/**
 * Default for -mempoolexpiry, expiration time for mempool transactions in
 * hours.
 */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 336;
/** Maximum number of dedicated script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 15;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;
static const bool DEFAULT_CHECKPOINTS_ENABLED = true;
static const bool DEFAULT_TXINDEX = false;
static constexpr bool DEFAULT_COINSTATSINDEX{false};
static const char *const DEFAULT_BLOCKFILTERINDEX = "0";

/** Default for -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;

static const bool DEFAULT_PEERBLOOMFILTERS = true;

/** Default for -stopatheight */
static const int DEFAULT_STOPATHEIGHT = 0;
/** Default for -maxreorgdepth */
static const int DEFAULT_MAX_REORG_DEPTH = 10;
/**
 * Default for -finalizationdelay
 * This is the minimum time between a block header reception and the block
 * finalization.
 * This value should be >> block propagation and validation time
 */
static const int64_t DEFAULT_MIN_FINALIZATION_DELAY = 2 * 60 * 60;
/**
 * Block files containing a block-height within MIN_BLOCKS_TO_KEEP of
 * ActiveChain().Tip() will not be pruned.
 */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;
static const signed int DEFAULT_CHECKBLOCKS = 6;
static const unsigned int DEFAULT_CHECKLEVEL = 3;
/**
 * Require that user allocate at least 550 MiB for block & undo files
 * (blk???.dat and rev???.dat)
 * At 1MB per block, 288 blocks = 288MB.
 * Add 15% for Undo data = 331MB
 * Add 20% for Orphan block rate = 397MB
 * We want the low water mark after pruning to be at least 397 MB and since we
 * prune in full block file chunks, we need the high water mark which triggers
 * the prune to be one 128MB block file + added 15% undo data = 147MB greater
 * for a total of 545MB
 * Setting the target to >= 550 MiB will make it likely we can respect the
 * target.
 */
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** Current sync state passed to tip changed callbacks. */
enum class SynchronizationState { INIT_REINDEX, INIT_DOWNLOAD, POST_INIT };

extern RecursiveMutex cs_main;
extern Mutex g_best_block_mutex;
extern std::condition_variable g_best_block_cv;
/** Used to notify getblocktemplate RPC of new tips. */
extern uint256 g_best_block;
extern bool fRequireStandard;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;

/**
 * A fee rate smaller than this is considered zero fee (for relaying, mining and
 * transaction creation)
 */
extern CFeeRate minRelayTxFee;
/**
 * If the tip is older than this (in seconds), the node is considered to be in
 * initial block download.
 */
extern int64_t nMaxTipAge;

/**
 * Block hash whose ancestors we will assume to have valid scripts without
 * checking them.
 */
extern BlockHash hashAssumeValid;

/**
 * Minimum work we will assume exists on some valid chain.
 */
extern arith_uint256 nMinimumChainWork;

/**
 * Best header we've seen so far (used for getheaders queries' starting points).
 */
extern CBlockIndex *pindexBestHeader;

/** Documentation for argument 'checklevel'. */
extern const std::vector<std::string> CHECKLEVEL_DOC;

class BlockValidationOptions {
private:
    uint64_t excessiveBlockSize;
    bool checkPoW : 1;
    bool checkMerkleRoot : 1;

public:
    // Do full validation by default
    explicit BlockValidationOptions(const Config &config);
    explicit BlockValidationOptions(uint64_t _excessiveBlockSize,
                                    bool _checkPow = true,
                                    bool _checkMerkleRoot = true)
        : excessiveBlockSize(_excessiveBlockSize), checkPoW(_checkPow),
          checkMerkleRoot(_checkMerkleRoot) {}

    BlockValidationOptions withCheckPoW(bool _checkPoW = true) const {
        BlockValidationOptions ret = *this;
        ret.checkPoW = _checkPoW;
        return ret;
    }

    BlockValidationOptions
    withCheckMerkleRoot(bool _checkMerkleRoot = true) const {
        BlockValidationOptions ret = *this;
        ret.checkMerkleRoot = _checkMerkleRoot;
        return ret;
    }

    bool shouldValidatePoW() const { return checkPoW; }
    bool shouldValidateMerkleRoot() const { return checkMerkleRoot; }
    uint64_t getExcessiveBlockSize() const { return excessiveBlockSize; }
};

/**
 * Unload database information.
 */
void UnloadBlockIndex(CTxMemPool *mempool, ChainstateManager &chainman);

/**
 * Run instances of script checking worker threads
 */
void StartScriptCheckWorkerThreads(int threads_num);

/**
 * Stop all of the script checking worker threads
 */
void StopScriptCheckWorkerThreads();

Amount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams);

bool AbortNode(BlockValidationState &state, const std::string &strMessage,
               const bilingual_str &userMessage = bilingual_str{});

/**
 * Guess verification progress (as a fraction between 0.0=genesis and
 * 1.0=current tip).
 */
double GuessVerificationProgress(const ChainTxData &data,
                                 const CBlockIndex *pindex);

/** Prune block files up to a given height */
void PruneBlockFilesManual(CChainState &active_chainstate,
                           int nManualPruneHeight);

/**
 * Validation result for a single transaction mempool acceptance.
 */
struct MempoolAcceptResult {
    /** Used to indicate the results of mempool validation. */
    enum class ResultType {
        //! Fully validated, valid.
        VALID,
        //! Invalid.
        INVALID,
        //! Valid, transaction was already in the mempool.
        MEMPOOL_ENTRY,
    };
    const ResultType m_result_type;
    const TxValidationState m_state;

    // The following fields are only present when m_result_type =
    // ResultType::VALID or MEMPOOL_ENTRY
    /**
     * Virtual size as used by the mempool, calculated using serialized size
     * and sigchecks.
     */
    const std::optional<int64_t> m_vsize;
    /** Raw base fees in satoshis. */
    const std::optional<Amount> m_base_fees;
    static MempoolAcceptResult Failure(TxValidationState state) {
        return MempoolAcceptResult(state);
    }

    /** Constructor for success case */
    static MempoolAcceptResult Success(int64_t vsize, Amount fees) {
        return MempoolAcceptResult(ResultType::VALID, vsize, fees);
    }

    /**
     * Constructor for already-in-mempool case. It wouldn't replace any
     * transactions.
     */
    static MempoolAcceptResult MempoolTx(int64_t vsize, Amount fees) {
        return MempoolAcceptResult(ResultType::MEMPOOL_ENTRY, vsize, fees);
    }

    // Private constructors. Use static methods MempoolAcceptResult::Success,
    // etc. to construct.
private:
    /** Constructor for failure case */
    explicit MempoolAcceptResult(TxValidationState state)
        : m_result_type(ResultType::INVALID), m_state(state),
          m_base_fees(std::nullopt) {
        // Can be invalid or error
        Assume(!state.IsValid());
    }

    /** Generic constructor for success cases */
    explicit MempoolAcceptResult(ResultType result_type, int64_t vsize,
                                 Amount fees)
        : m_result_type(result_type), m_vsize{vsize}, m_base_fees(fees) {}
};

/**
 * Validation result for package mempool acceptance.
 */
struct PackageMempoolAcceptResult {
    const PackageValidationState m_state;
    /**
     * Map from txid to finished MempoolAcceptResults. The client is
     * responsible for keeping track of the transaction objects themselves.
     * If a result is not present, it means validation was unfinished for that
     * transaction. If there was a package-wide error (see result in m_state),
     * m_tx_results will be empty.
     */
    std::map<const TxId, const MempoolAcceptResult> m_tx_results;

    explicit PackageMempoolAcceptResult(
        PackageValidationState state,
        std::map<const TxId, const MempoolAcceptResult> &&results)
        : m_state{state}, m_tx_results(std::move(results)) {}

    /**
     * Constructor to create a PackageMempoolAcceptResult from a
     * MempoolAcceptResult
     */
    explicit PackageMempoolAcceptResult(const TxId &txid,
                                        const MempoolAcceptResult &result)
        : m_tx_results{{txid, result}} {}
};

/**
 * Try to add a transaction to the mempool. This is an internal function and is
 * exposed only for testing. Client code should use
 * ChainstateManager::ProcessTransaction()
 *
 * @param[in]  config             The global configuration.
 * @param[in]  active_chainstate  Reference to the active chainstate.
 * @param[in]  tx                 The transaction to submit for mempool
 *                                acceptance.
 * @param[in]  accept_time        The timestamp for adding the transaction to
 *                                the mempool.
 *                                It is also used to determine when the entry
 *                                expires.
 * @param[in]  bypass_limits      When true, don't enforce mempool fee and
 *                                capacity limits.
 * @param[in]  test_accept        When true, run validation checks but don't
 *                                submit to mempool.
 *
 * @returns a MempoolAcceptResult indicating whether the transaction was
 *     accepted/rejected with reason.
 */
MempoolAcceptResult
AcceptToMemoryPool(const Config &config, CChainState &active_chainstate,
                   const CTransactionRef &tx, int64_t accept_time,
                   bool bypass_limits, bool test_accept = false)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * Validate (and maybe submit) a package to the mempool.
 * See doc/policy/packages.md for full detailson package validation rules.
 *
 * @param[in]    test_accept     When true, run validation checks but don't
 *                               submit to mempool.
 * @returns a PackageMempoolAcceptResult which includes a MempoolAcceptResult
 *     for each transaction. If a transaction fails, validation will exit early
 *     and some results may be missing. It is also possible for the package to
 *     be partially submitted.
 */
PackageMempoolAcceptResult
ProcessNewPackage(const Config &config, CChainState &active_chainstate,
                  CTxMemPool &pool, const Package &txns, bool test_accept)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * Simple class for regulating resource usage during CheckInputScripts (and
 * CScriptCheck), atomic so as to be compatible with parallel validation.
 */
class CheckInputsLimiter {
protected:
    std::atomic<int64_t> remaining;

public:
    explicit CheckInputsLimiter(int64_t limit) : remaining(limit) {}

    bool consume_and_check(int consumed) {
        auto newvalue = (remaining -= consumed);
        return newvalue >= 0;
    }

    bool check() { return remaining >= 0; }
};

class TxSigCheckLimiter : public CheckInputsLimiter {
public:
    TxSigCheckLimiter() : CheckInputsLimiter(MAX_TX_SIGCHECKS) {}

    // Let's make this bad boy copiable.
    TxSigCheckLimiter(const TxSigCheckLimiter &rhs)
        : CheckInputsLimiter(rhs.remaining.load()) {}

    TxSigCheckLimiter &operator=(const TxSigCheckLimiter &rhs) {
        remaining = rhs.remaining.load();
        return *this;
    }

    static TxSigCheckLimiter getDisabled() {
        TxSigCheckLimiter txLimiter;
        // Historically, there has not been a transaction with more than 20k sig
        // checks on testnet or mainnet, so this effectively disable sigchecks.
        txLimiter.remaining = 20000;
        return txLimiter;
    }
};

class ConnectTrace;

/**
 * Check whether all of this transaction's input scripts succeed.
 *
 * This involves ECDSA signature checks so can be computationally intensive.
 * This function should only be called after the cheap sanity checks in
 * CheckTxInputs passed.
 *
 * If pvChecks is not nullptr, script checks are pushed onto it instead of being
 * performed inline. Any script checks which are not necessary (eg due to script
 * execution cache hits) are, obviously, not pushed onto pvChecks/run.
 *
 * Upon success nSigChecksOut will be filled in with either:
 * - correct total for all inputs, or,
 * - 0, in the case when checks were pushed onto pvChecks (i.e., a cache miss
 * with pvChecks non-null), in which case the total can be found by executing
 * pvChecks and adding the results.
 *
 * Setting sigCacheStore/scriptCacheStore to false will remove elements from the
 * corresponding cache which are matched. This is useful for checking blocks
 * where we will likely never need the cache entry again.
 *
 * pLimitSigChecks can be passed to limit the sigchecks count either in parallel
 * or serial validation. With pvChecks null (serial validation), breaking the
 * pLimitSigChecks limit will abort evaluation early and return false. With
 * pvChecks not-null (parallel validation): the cached nSigChecks may itself
 * break the limit in which case false is returned, OR, each entry in the
 * returned pvChecks must be executed exactly once in order to probe the limit
 * accurately.
 */
bool CheckInputScripts(const CTransaction &tx, TxValidationState &state,
                       const CCoinsViewCache &view, const uint32_t flags,
                       bool sigCacheStore, bool scriptCacheStore,
                       const PrecomputedTransactionData &txdata,
                       int &nSigChecksOut, TxSigCheckLimiter &txLimitSigChecks,
                       CheckInputsLimiter *pBlockLimitSigChecks,
                       std::vector<CScriptCheck> *pvChecks)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * Handy shortcut to full fledged CheckInputScripts call.
 */
static inline bool
CheckInputScripts(const CTransaction &tx, TxValidationState &state,
                  const CCoinsViewCache &view, const uint32_t flags,
                  bool sigCacheStore, bool scriptCacheStore,
                  const PrecomputedTransactionData &txdata, int &nSigChecksOut)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
    TxSigCheckLimiter nSigChecksTxLimiter;
    return CheckInputScripts(tx, state, view, flags, sigCacheStore,
                             scriptCacheStore, txdata, nSigChecksOut,
                             nSigChecksTxLimiter, nullptr, nullptr);
}

/**
 * Mark all the coins corresponding to a given transaction inputs as spent.
 */
void SpendCoins(CCoinsViewCache &view, const CTransaction &tx, CTxUndo &txundo,
                int nHeight);

/**
 * Apply the effects of this transaction on the UTXO set represented by view.
 */
void UpdateCoins(CCoinsViewCache &view, const CTransaction &tx, CTxUndo &txundo,
                 int nHeight);

/**
 * Check if transaction will be BIP68 final in the next block to be created on
 * top of tip.
 * @param[in]   tip             Chain tip to check tx sequence locks against.
 *     For example, the tip of the current active chain.
 * @param[in]   coins_view      Any CCoinsView that provides access to the
 *     relevant coins for checking sequence locks. For example, it can be a
 *     CCoinsViewCache that isn't connected to anything but contains all the
 *     relevant coins, or a CCoinsViewMemPool that is connected to the mempool
 *     and chainstate UTXO set. In the latter case, the caller is responsible
 *     for holding the appropriate locks to ensure that calls to GetCoin()
 *     return correct coins.
 * Simulates calling SequenceLocks() with data from the tip passed in.
 * Optionally stores in LockPoints the resulting height and time
 * calculated and the hash of the block needed for calculation or skips the
 * calculation and uses the LockPoints passed in for evaluation. The LockPoints
 * should not be considered valid if CheckSequenceLocksAtTip returns false.
 */
bool CheckSequenceLocksAtTip(CBlockIndex *tip, const CCoinsView &coins_view,
                             const CTransaction &tx, LockPoints *lp = nullptr,
                             bool useExistingLockPoints = false);

/**
 * Closure representing one script verification.
 * Note that this stores references to the spending transaction.
 *
 * Note that if pLimitSigChecks is passed, then failure does not imply that
 * scripts have failed.
 */
class CScriptCheck {
private:
    CTxOut m_tx_out;
    const CTransaction *ptxTo;
    unsigned int nIn;
    uint32_t nFlags;
    bool cacheStore;
    ScriptError error;
    ScriptExecutionMetrics metrics;
    PrecomputedTransactionData txdata;
    TxSigCheckLimiter *pTxLimitSigChecks;
    CheckInputsLimiter *pBlockLimitSigChecks;

public:
    CScriptCheck()
        : ptxTo(nullptr), nIn(0), nFlags(0), cacheStore(false),
          error(ScriptError::UNKNOWN), txdata(), pTxLimitSigChecks(nullptr),
          pBlockLimitSigChecks(nullptr) {}

    CScriptCheck(const CTxOut &outIn, const CTransaction &txToIn,
                 unsigned int nInIn, uint32_t nFlagsIn, bool cacheIn,
                 const PrecomputedTransactionData &txdataIn,
                 TxSigCheckLimiter *pTxLimitSigChecksIn = nullptr,
                 CheckInputsLimiter *pBlockLimitSigChecksIn = nullptr)
        : m_tx_out(outIn), ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn),
          cacheStore(cacheIn), error(ScriptError::UNKNOWN), txdata(txdataIn),
          pTxLimitSigChecks(pTxLimitSigChecksIn),
          pBlockLimitSigChecks(pBlockLimitSigChecksIn) {}

    bool operator()();

    void swap(CScriptCheck &check) {
        std::swap(ptxTo, check.ptxTo);
        std::swap(m_tx_out, check.m_tx_out);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
        std::swap(metrics, check.metrics);
        std::swap(txdata, check.txdata);
        std::swap(pTxLimitSigChecks, check.pTxLimitSigChecks);
        std::swap(pBlockLimitSigChecks, check.pBlockLimitSigChecks);
    }

    ScriptError GetScriptError() const { return error; }

    ScriptExecutionMetrics GetScriptExecutionMetrics() const { return metrics; }
};

/** Functions for validating blocks and updating the block tree */

/**
 * Context-independent validity checks.
 *
 * Returns true if the provided block is valid (has valid header,
 * transactions are valid, block is a valid size, etc.)
 */
bool CheckBlock(const CBlock &block, BlockValidationState &state,
                const Consensus::Params &params,
                BlockValidationOptions validationOptions);

/**
 * This is a variant of ContextualCheckTransaction which computes the contextual
 * check for a transaction based on the chain tip.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool ContextualCheckTransactionForCurrentBlock(
    const CBlockIndex *active_chain_tip, const Consensus::Params &params,
    const CTransaction &tx, TxValidationState &state)
    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

/**
 * Check a block is completely valid from start to finish (only works on top of
 * our current best block)
 */
bool TestBlockValidity(BlockValidationState &state, const CChainParams &params,
                       CChainState &chainstate, const CBlock &block,
                       CBlockIndex *pindexPrev,
                       BlockValidationOptions validationOptions)
    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * RAII wrapper for VerifyDB: Verify consistency of the block and coin
 * databases.
 */
class CVerifyDB {
public:
    CVerifyDB();

    ~CVerifyDB();

    bool VerifyDB(CChainState &chainstate, const Config &config,
                  CCoinsView &coinsview, int nCheckLevel, int nCheckDepth)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

/** @see CChainState::FlushStateToDisk */
enum class FlushStateMode { NONE, IF_NEEDED, PERIODIC, ALWAYS };

/**
 * A convenience class for constructing the CCoinsView* hierarchy used
 * to facilitate access to the UTXO set.
 *
 * This class consists of an arrangement of layered CCoinsView objects,
 * preferring to store and retrieve coins in memory via `m_cacheview` but
 * ultimately falling back on cache misses to the canonical store of UTXOs on
 * disk, `m_dbview`.
 */
class CoinsViews {
public:
    //! The lowest level of the CoinsViews cache hierarchy sits in a leveldb
    //! database on disk. All unspent coins reside in this store.
    CCoinsViewDB m_dbview GUARDED_BY(cs_main);

    //! This view wraps access to the leveldb instance and handles read errors
    //! gracefully.
    CCoinsViewErrorCatcher m_catcherview GUARDED_BY(cs_main);

    //! This is the top layer of the cache hierarchy - it keeps as many coins in
    //! memory as can fit per the dbcache setting.
    std::unique_ptr<CCoinsViewCache> m_cacheview GUARDED_BY(cs_main);

    //! This constructor initializes CCoinsViewDB and CCoinsViewErrorCatcher
    //! instances, but it *does not* create a CCoinsViewCache instance by
    //! default. This is done separately because the presence of the cache has
    //! implications on whether or not we're allowed to flush the cache's state
    //! to disk, which should not be done until the health of the database is
    //! verified.
    //!
    //! All arguments forwarded onto CCoinsViewDB.
    CoinsViews(std::string ldb_name, size_t cache_size_bytes, bool in_memory,
               bool should_wipe);

    //! Initialize the CCoinsViewCache member.
    void InitCache() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

enum class CoinsCacheSizeState {
    //! The coins cache is in immediate need of a flush.
    CRITICAL = 2,
    //! The cache is at >= 90% capacity.
    LARGE = 1,
    OK = 0
};

/**
 * CChainState stores and provides an API to update our local knowledge of the
 * current best chain.
 *
 * Eventually, the API here is targeted at being exposed externally as a
 * consumable libconsensus library, so any functions added must only call
 * other class member functions, pure functions in other parts of the consensus
 * library, callbacks via the validation interface, or read/write-to-disk
 * functions (eventually this will also be via callbacks).
 *
 * Anything that is contingent on the current tip of the chain is stored here,
 * whereas block information and metadata independent of the current tip is
 * kept in `BlockManager`.
 */
class CChainState {
private:
    /**
     * The ChainState Mutex.
     * A lock that must be held when modifying this ChainState.
     */
    Mutex m_chainstate_mutex;

    /**
     * Every received block is assigned a unique and increasing identifier, so
     * we know which one to give priority in case of a fork.
     * Blocks loaded from disk are assigned id 0, so start the counter at 1.
     */
    std::atomic<int32_t> nBlockSequenceId{1};
    /** Decreasing counter (used by subsequent preciousblock calls). */
    int32_t nBlockReverseSequenceId = -1;
    /** chainwork for the last block that preciousblock has been applied to. */
    arith_uint256 nLastPreciousChainwork = 0;

    /**
     * Whether this chainstate is undergoing initial block download.
     *
     * Mutable because we need to be able to mark IsInitialBlockDownload()
     * const, which latches this for caching purposes.
     */
    mutable std::atomic<bool> m_cached_finished_ibd{false};

    //! Optional mempool that is kept in sync with the chain.
    //! Only the active chainstate has a mempool.
    CTxMemPool *m_mempool;

    //! Manages the UTXO set, which is a reflection of the contents of
    //! `m_chain`.
    std::unique_ptr<CoinsViews> m_coins_views;

    /**
     * The best finalized block.
     * This block cannot be reorged in any way except by explicit user action.
     */
    const CBlockIndex *m_finalizedBlockIndex GUARDED_BY(cs_main) = nullptr;

    mutable Mutex cs_avalancheFinalizedBlockIndex;

    /**
     * The best block via avalanche voting.
     * This block cannot be reorged in any way except by explicit user action.
     */
    const CBlockIndex *m_avalancheFinalizedBlockIndex
        GUARDED_BY(cs_avalancheFinalizedBlockIndex) = nullptr;

public:
    //! Reference to a BlockManager instance which itself is shared across all
    //! CChainState instances.
    node::BlockManager &m_blockman;

    /** Chain parameters for this chainstate */
    const CChainParams &m_params;

    //! The chainstate manager that owns this chainstate. The reference is
    //! necessary so that this instance can check whether it is the active
    //! chainstate within deeply nested method calls.
    ChainstateManager &m_chainman;

    explicit CChainState(
        CTxMemPool *mempool, node::BlockManager &blockman,
        ChainstateManager &chainman,
        std::optional<BlockHash> from_snapshot_blockhash = std::nullopt);

    /**
     * Initialize the CoinsViews UTXO set database management data structures.
     * The in-memory cache is initialized separately.
     *
     * All parameters forwarded to CoinsViews.
     */
    void InitCoinsDB(size_t cache_size_bytes, bool in_memory, bool should_wipe,
                     std::string leveldb_name = "chainstate");

    //! Initialize the in-memory coins cache (to be done after the health of the
    //! on-disk database is verified).
    void InitCoinsCache(size_t cache_size_bytes)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! @returns whether or not the CoinsViews object has been fully initialized
    //! and we can
    //!          safely flush this object to disk.
    bool CanFlushToDisk() const EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        AssertLockHeld(::cs_main);
        return m_coins_views && m_coins_views->m_cacheview;
    }

    //! The current chain of blockheaders we consult and build on.
    //! @see CChain, CBlockIndex.
    CChain m_chain;

    /**
     * The blockhash which is the base of the snapshot this chainstate was
     * created from.
     *
     * std::nullopt if this chainstate was not created from a snapshot.
     */
    const std::optional<BlockHash> m_from_snapshot_blockhash{};

    //! Return true if this chainstate relies on blocks that are assumed-valid.
    //! In practice this means it was created based on a UTXO snapshot.
    bool reliesOnAssumedValid() {
        return m_from_snapshot_blockhash.has_value();
    }

    /**
     * The set of all CBlockIndex entries with either BLOCK_VALID_TRANSACTIONS
     * (for itself and all ancestors) *or* BLOCK_ASSUMED_VALID (if using
     * background chainstates) and as good as our current tip or better.
     * Entries may be failed, though, and pruning nodes may be missing the data
     * for the block.
     */
    std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates;

    //! @returns A reference to the in-memory cache of the UTXO set.
    CCoinsViewCache &CoinsTip() EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        AssertLockHeld(::cs_main);
        assert(m_coins_views->m_cacheview);
        return *m_coins_views->m_cacheview.get();
    }

    //! @returns A reference to the on-disk UTXO set database.
    CCoinsViewDB &CoinsDB() EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        AssertLockHeld(::cs_main);
        return m_coins_views->m_dbview;
    }

    //! @returns A pointer to the mempool.
    CTxMemPool *GetMempool() { return m_mempool; }

    //! @returns A reference to a wrapped view of the in-memory UTXO set that
    //!     handles disk read errors gracefully.
    CCoinsViewErrorCatcher &CoinsErrorCatcher()
        EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
        AssertLockHeld(::cs_main);
        return m_coins_views->m_catcherview;
    }

    //! Destructs all objects related to accessing the UTXO set.
    void ResetCoinsViews() { m_coins_views.reset(); }

    //! The cache size of the on-disk coins view.
    size_t m_coinsdb_cache_size_bytes{0};

    //! The cache size of the in-memory coins view.
    size_t m_coinstip_cache_size_bytes{0};

    //! Resize the CoinsViews caches dynamically and flush state to disk.
    //! @returns true unless an error occurred during the flush.
    bool ResizeCoinsCaches(size_t coinstip_size, size_t coinsdb_size)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    /** Import blocks from an external file */
    void LoadExternalBlockFile(const Config &config, FILE *fileIn,
                               FlatFilePos *dbp = nullptr)
        EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex);

    /**
     * Update the on-disk chain state.
     * The caches and indexes are flushed depending on the mode we're called
     * with if they're too large, if it's been a while since the last write, or
     * always and in all cases if we're in prune mode and are deleting files.
     *
     * If FlushStateMode::NONE is used, then FlushStateToDisk(...) won't do
     * anything besides checking if we need to prune.
     *
     * @returns true unless a system error occurred
     */
    bool FlushStateToDisk(BlockValidationState &state, FlushStateMode mode,
                          int nManualPruneHeight = 0);

    //! Unconditionally flush all changes to disk.
    void ForceFlushStateToDisk();

    //! Prune blockfiles from the disk if necessary and then flush chainstate
    //! changes if we pruned.
    void PruneAndFlush();

    /**
     * Find the best known block, and make it the tip of the block chain. The
     * result is either failure or an activated best chain. pblock is either
     * nullptr or a pointer to a block that is already loaded (to avoid loading
     * it again from disk).
     *
     * ActivateBestChain is split into steps (see ActivateBestChainStep) so that
     * we avoid holding cs_main for an extended period of time; the length of
     * this call may be quite long during reindexing or a substantial reorg.
     *
     * May not be called with cs_main held. May not be called in a
     * validationinterface callback.
     *
     * @returns true unless a system error occurred
     */
    bool ActivateBestChain(const Config &config, BlockValidationState &state,
                           std::shared_ptr<const CBlock> pblock = nullptr)
        EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex) LOCKS_EXCLUDED(cs_main);

    bool AcceptBlock(const Config &config,
                     const std::shared_ptr<const CBlock> &pblock,
                     BlockValidationState &state, bool fRequested,
                     const FlatFilePos *dbp, bool *fNewBlock)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // Block (dis)connection on a given view:
    DisconnectResult DisconnectBlock(const CBlock &block,
                                     const CBlockIndex *pindex,
                                     CCoinsViewCache &view)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool ConnectBlock(const CBlock &block, BlockValidationState &state,
                      CBlockIndex *pindex, CCoinsViewCache &view,
                      BlockValidationOptions options, bool fJustCheck = false)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // Apply the effects of a block disconnection on the UTXO set.
    bool DisconnectTip(BlockValidationState &state,
                       DisconnectedBlockTransactions *disconnectpool)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool->cs);

    // Manual block validity manipulation:
    /**
     * Mark a block as precious and reorganize.
     *
     * May not be called in a validationinterface callback.
     */
    bool PreciousBlock(const Config &config, BlockValidationState &state,
                       CBlockIndex *pindex)
        EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex) LOCKS_EXCLUDED(cs_main);
    /** Mark a block as invalid. */
    bool InvalidateBlock(const Config &config, BlockValidationState &state,
                         CBlockIndex *pindex) LOCKS_EXCLUDED(cs_main)
        EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex);
    /** Park a block. */
    bool ParkBlock(const Config &config, BlockValidationState &state,
                   CBlockIndex *pindex) LOCKS_EXCLUDED(cs_main)
        EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex);

    /**
     * Finalize a block.
     * A finalized block can not be reorged in any way.
     */
    bool FinalizeBlock(const Config &config, BlockValidationState &state,
                       CBlockIndex *pindex) LOCKS_EXCLUDED(cs_main)
        EXCLUSIVE_LOCKS_REQUIRED(!m_chainstate_mutex);
    /** Return the currently finalized block index. */
    const CBlockIndex *GetFinalizedBlock() const
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /**
     * Checks if a block is finalized.
     */
    bool IsBlockFinalized(const CBlockIndex *pindex) const
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Mark a block as finalized by avalanche.
     */
    bool AvalancheFinalizeBlock(CBlockIndex *pindex);

    /**
     * Clear avalanche finalization.
     */
    void ClearAvalancheFinalizedBlock();

    /**
     * Checks if a block is finalized by avalanche voting.
     */
    bool IsBlockAvalancheFinalized(const CBlockIndex *pindex) const;

    /** Remove invalidity status from a block and its descendants. */
    void ResetBlockFailureFlags(CBlockIndex *pindex)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    template <typename F>
    bool UpdateFlagsForBlock(CBlockIndex *pindexBase, CBlockIndex *pindex, F f)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    template <typename F, typename C, typename AC>
    void UpdateFlags(CBlockIndex *pindex, CBlockIndex *&pindexReset, F f,
                     C fChild, AC fAncestorWasChanged)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Remove parked status from a block and its descendants. */
    void UnparkBlockAndChildren(CBlockIndex *pindex)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Remove parked status from a block. */
    void UnparkBlock(CBlockIndex *pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Replay blocks that aren't fully applied to the database. */
    bool ReplayBlocks();

    /**
     * Ensures we have a genesis block in the block tree, possibly writing one
     * to disk.
     */
    bool LoadGenesisBlock();

    void PruneBlockIndexCandidates();

    void UnloadBlockIndex() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Check whether we are doing an initial block download (synchronizing from
     * disk or network)
     */
    bool IsInitialBlockDownload() const;

    /** Find the last common block of this chain and a locator. */
    CBlockIndex *FindForkInGlobalIndex(const CBlockLocator &locator) const
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Make various assertions about the state of the block index.
     *
     * By default this only executes fully when using the Regtest chain; see:
     * fCheckBlockIndex.
     */
    void CheckBlockIndex();

    /** Load the persisted mempool from disk */
    void LoadMempool(const Config &config, const ArgsManager &args);

    /** Update the chain tip based on database information, i.e. CoinsTip()'s
     * best block. */
    bool LoadChainTip() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Dictates whether we need to flush the cache to disk or not.
    //!
    //! @return the state of the size of the coins cache.
    CoinsCacheSizeState GetCoinsCacheSizeState()
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    CoinsCacheSizeState
    GetCoinsCacheSizeState(size_t max_coins_cache_size_bytes,
                           size_t max_mempool_size_bytes)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    std::string ToString() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    bool ActivateBestChainStep(const Config &config,
                               BlockValidationState &state,
                               CBlockIndex *pindexMostWork,
                               const std::shared_ptr<const CBlock> &pblock,
                               bool &fInvalidFound, ConnectTrace &connectTrace)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool->cs);
    bool ConnectTip(const Config &config, BlockValidationState &state,
                    CBlockIndex *pindexNew,
                    const std::shared_ptr<const CBlock> &pblock,
                    ConnectTrace &connectTrace,
                    DisconnectedBlockTransactions &disconnectpool)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_mempool->cs);
    void InvalidBlockFound(CBlockIndex *pindex,
                           const BlockValidationState &state)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    CBlockIndex *FindMostWorkChain() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool MarkBlockAsFinal(BlockValidationState &state,
                          const CBlockIndex *pindex)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ReceivedBlockTransactions(const CBlock &block, CBlockIndex *pindexNew,
                                   const FlatFilePos &pos)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool RollforwardBlock(const CBlockIndex *pindex, CCoinsViewCache &inputs)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void UnparkBlockImpl(CBlockIndex *pindex, bool fClearChildren)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool UnwindBlock(const Config &config, BlockValidationState &state,
                     CBlockIndex *pindex, bool invalidate)
        EXCLUSIVE_LOCKS_REQUIRED(m_chainstate_mutex);

    void CheckForkWarningConditions() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void CheckForkWarningConditionsOnNewFork(CBlockIndex *pindexNewForkTip)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void InvalidChainFound(CBlockIndex *pindexNew)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    const CBlockIndex *FindBlockToFinalize(CBlockIndex *pindexNew)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Indirection necessary to make lock annotations work with an optional
    //! mempool.
    RecursiveMutex *MempoolMutex() const LOCK_RETURNED(m_mempool->cs) {
        return m_mempool ? &m_mempool->cs : nullptr;
    }

    /**
     * Check warning conditions and do some notifications on new chain tip set.
     */
    void UpdateTip(const CBlockIndex *pindexNew)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    friend ChainstateManager;
};

/**
 * Provides an interface for creating and interacting with one or two
 * chainstates: an IBD chainstate generated by downloading blocks, and
 * an optional snapshot chainstate loaded from a UTXO snapshot. Managed
 * chainstates can be maintained at different heights simultaneously.
 *
 * This class provides abstractions that allow the retrieval of the current
 * most-work chainstate ("Active") as well as chainstates which may be in
 * background use to validate UTXO snapshots.
 *
 * Definitions:
 *
 * *IBD chainstate*: a chainstate whose current state has been "fully"
 *   validated by the initial block download process.
 *
 * *Snapshot chainstate*: a chainstate populated by loading in an
 *    assumeutxo UTXO snapshot.
 *
 * *Active chainstate*: the chainstate containing the current most-work
 *    chain. Consulted by most parts of the system (net_processing,
 *    wallet) as a reflection of the current chain and UTXO set.
 *    This may either be an IBD chainstate or a snapshot chainstate.
 *
 * *Background IBD chainstate*: an IBD chainstate for which the
 *    IBD process is happening in the background while use of the
 *    active (snapshot) chainstate allows the rest of the system to function.
 */
class ChainstateManager {
private:
    //! The chainstate used under normal operation (i.e. "regular" IBD) or, if
    //! a snapshot is in use, for background validation.
    //!
    //! Its contents (including on-disk data) will be deleted *upon shutdown*
    //! after background validation of the snapshot has completed. We do not
    //! free the chainstate contents immediately after it finishes validation
    //! to cautiously avoid a case where some other part of the system is still
    //! using this pointer (e.g. net_processing).
    //!
    //! Once this pointer is set to a corresponding chainstate, it will not
    //! be reset until init.cpp:Shutdown().
    //!
    //! This is especially important when, e.g., calling ActivateBestChain()
    //! on all chainstates because we are not able to hold ::cs_main going into
    //! that call.
    std::unique_ptr<CChainState> m_ibd_chainstate GUARDED_BY(::cs_main);

    //! A chainstate initialized on the basis of a UTXO snapshot. If this is
    //! non-null, it is always our active chainstate.
    //!
    //! Once this pointer is set to a corresponding chainstate, it will not
    //! be reset until init.cpp:Shutdown().
    //!
    //! This is especially important when, e.g., calling ActivateBestChain()
    //! on all chainstates because we are not able to hold ::cs_main going into
    //! that call.
    std::unique_ptr<CChainState> m_snapshot_chainstate GUARDED_BY(::cs_main);

    //! Points to either the ibd or snapshot chainstate; indicates our
    //! most-work chain.
    //!
    //! Once this pointer is set to a corresponding chainstate, it will not
    //! be reset until init.cpp:Shutdown().
    //!
    //! This is especially important when, e.g., calling ActivateBestChain()
    //! on all chainstates because we are not able to hold ::cs_main going into
    //! that call.
    CChainState *m_active_chainstate GUARDED_BY(::cs_main){nullptr};

    //! If true, the assumed-valid chainstate has been fully validated
    //! by the background validation chainstate.
    bool m_snapshot_validated{false};

    CBlockIndex *m_best_invalid;
    CBlockIndex *m_best_parked;
    friend bool node::BlockManager::LoadBlockIndex(const Consensus::Params &,
                                                   ChainstateManager &);

    //! Internal helper for ActivateSnapshot().
    [[nodiscard]] bool
    PopulateAndValidateSnapshot(CChainState &snapshot_chainstate,
                                CAutoFile &coins_file,
                                const node::SnapshotMetadata &metadata);
    /**
     * If a block header hasn't already been seen, call CheckBlockHeader on it,
     * ensure that it doesn't descend from an invalid block, and then add it to
     * m_block_index.
     */
    bool AcceptBlockHeader(const Config &config, const CBlockHeader &block,
                           BlockValidationState &state, CBlockIndex **ppindex)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    friend CChainState;

public:
    std::thread m_load_block;
    //! A single BlockManager instance is shared across each constructed
    //! chainstate to avoid duplicating block metadata.
    node::BlockManager m_blockman;

    /**
     * In order to efficiently track invalidity of headers, we keep the set of
     * blocks which we tried to connect and found to be invalid here (ie which
     * were set to BLOCK_FAILED_VALID since the last restart). We can then
     * walk this set and check if a new header is a descendant of something in
     * this set, preventing us from having to walk m_block_index when we try
     * to connect a bad block and fail.
     *
     * While this is more complicated than marking everything which descends
     * from an invalid block as invalid at the time we discover it to be
     * invalid, doing so would require walking all of m_block_index to find all
     * descendants. Since this case should be very rare, keeping track of all
     * BLOCK_FAILED_VALID blocks in a set should be just fine and work just as
     * well.
     *
     * Because we already walk m_block_index in height-order at startup, we go
     * ahead and mark descendants of invalid blocks as FAILED_CHILD at that
     * time, instead of putting things in this set.
     */
    std::set<CBlockIndex *> m_failed_blocks;

    //! The total number of bytes available for us to use across all in-memory
    //! coins caches. This will be split somehow across chainstates.
    int64_t m_total_coinstip_cache{0};
    //
    //! The total number of bytes available for us to use across all leveldb
    //! coins databases. This will be split somehow across chainstates.
    int64_t m_total_coinsdb_cache{0};

    //! Instantiate a new chainstate and assign it based upon whether it is
    //! from a snapshot.
    //!
    //! @param[in] mempool              The mempool to pass to the chainstate
    //                                  constructor
    //! @param[in] snapshot_blockhash   If given, signify that this chainstate
    //!                                 is based on a snapshot.
    CChainState &
    InitializeChainstate(CTxMemPool *mempool,
                         const std::optional<BlockHash> &snapshot_blockhash =
                             std::nullopt) LIFETIMEBOUND
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! Get all chainstates currently being used.
    std::vector<CChainState *> GetAll();

    //! Construct and activate a Chainstate on the basis of UTXO snapshot data.
    //!
    //! Steps:
    //!
    //! - Initialize an unused CChainState.
    //! - Load its `CoinsViews` contents from `coins_file`.
    //! - Verify that the hash of the resulting coinsdb matches the expected
    //!   hash per assumeutxo chain parameters.
    //! - Wait for our headers chain to include the base block of the snapshot.
    //! - "Fast forward" the tip of the new chainstate to the base of the
    //!   snapshot, faking nTx* block index data along the way.
    //! - Move the new chainstate to `m_snapshot_chainstate` and make it our
    //!   ActiveChainstate().
    [[nodiscard]] bool ActivateSnapshot(CAutoFile &coins_file,
                                        const node::SnapshotMetadata &metadata,
                                        bool in_memory);

    //! The most-work chain.
    CChainState &ActiveChainstate() const;
    CChain &ActiveChain() const { return ActiveChainstate().m_chain; }
    int ActiveHeight() const { return ActiveChain().Height(); }
    CBlockIndex *ActiveTip() const { return ActiveChain().Tip(); }

    node::BlockMap &BlockIndex() EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
        AssertLockHeld(::cs_main);
        return m_blockman.m_block_index;
    }

    //! @returns true if a snapshot-based chainstate is in use. Also implies
    //!          that a background validation chainstate is also in use.
    bool IsSnapshotActive() const;

    std::optional<BlockHash> SnapshotBlockhash() const;

    //! Is there a snapshot in use and has it been fully validated?
    bool IsSnapshotValidated() const { return m_snapshot_validated; }

    /**
     * Process an incoming block. This only returns after the best known valid
     * block is made active. Note that it does not, however, guarantee that the
     * specific block passed to it has been checked for validity!
     *
     * If you want to *possibly* get feedback on whether block is valid, you
     * must install a CValidationInterface (see validationinterface.h) - this
     * will have its BlockChecked method called whenever *any* block completes
     * validation.
     *
     * Note that we guarantee that either the proof-of-work is valid on block,
     * or (and possibly also) BlockChecked will have been called.
     *
     * May not be called in a validationinterface callback.
     *
     * @param[in]   config  The global config.
     * @param[in]   block  The block we want to process.
     * @param[in]   force_processing Process this block even if unrequested;
     *              used for non-network block sources.
     * @param[out]  new_block A boolean which is set to indicate if the block
     * was first received via this call.
     * @returns     If the block was processed, independently of block validity
     */
    bool ProcessNewBlock(const Config &config,
                         const std::shared_ptr<const CBlock> &block,
                         bool force_processing, bool *new_block)
        LOCKS_EXCLUDED(cs_main);

    /**
     * Process incoming block headers.
     *
     * May not be called in a validationinterface callback.
     *
     * @param[in]  config        The config.
     * @param[in]  block         The block headers themselves.
     * @param[out] state         This may be set to an Error state if any error
     *                           occurred processing them.
     * @param[out] ppindex       If set, the pointer will be set to point to the
     *                           last new block index object for the given
     * headers.
     * @return True if block headers were accepted as valid.
     */
    bool ProcessNewBlockHeaders(const Config &config,
                                const std::vector<CBlockHeader> &block,
                                BlockValidationState &state,
                                const CBlockIndex **ppindex = nullptr)
        LOCKS_EXCLUDED(cs_main);

    /**
     * Try to add a transaction to the memory pool.
     *
     * @param[in]  tx              The transaction to submit for mempool
     *                             acceptance.
     * @param[in]  test_accept     When true, run validation checks but don't
     *                             submit to mempool.
     */
    [[nodiscard]] MempoolAcceptResult
    ProcessTransaction(const CTransactionRef &tx, bool test_accept = false)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Load the block tree and coins database from disk, initializing state if
    //! we're running with -reindex
    bool LoadBlockIndex() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Unload block index and chain data before shutdown.
    void Unload() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! Clear (deconstruct) chainstate data.
    void Reset();

    //! Check to see if caches are out of balance and if so, call
    //! ResizeCoinsCaches() as needed.
    void MaybeRebalanceCaches() EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    ~ChainstateManager() {
        LOCK(::cs_main);
        UnloadBlockIndex(/* mempool */ nullptr, *this);
        Reset();
    }
};

/** Dump the mempool to disk. */
bool DumpMempool(const CTxMemPool &pool);

/** Load the mempool from disk. */
bool LoadMempool(const Config &config, CTxMemPool &pool,
                 CChainState &active_chainstate);

/**
 * Return the expected assumeutxo value for a given height, if one exists.
 *
 * @param[in] height Get the assumeutxo value for this height.
 *
 * @returns empty if no assumeutxo configuration exists for the given height.
 */
const AssumeutxoData *ExpectedAssumeutxo(const int height,
                                         const CChainParams &params);

#endif // BITCOIN_VALIDATION_H
