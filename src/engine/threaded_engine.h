/*!
 * Copyright (c) 2015 by Contributors
 * \file threaded_engine.h
 * \brief Implements base class of threaded engine
 *    that tracks the dependency and pushes actions to execute.
 * \author Yutian Li
 */
#ifndef MXNET_ENGINE_THREADED_ENGINE_H_
#define MXNET_ENGINE_THREADED_ENGINE_H_

#include <dmlc/base.h>
#include <dmlc/logging.h>
#include <vector>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include "./engine_impl.h"
#include "../common/object_pool.h"

namespace mxnet {
namespace engine {

// Define helper macros for debug information.
#if ENGINE_DEBUG
#define DEFINE_ENGINE_DEBUG_INFO(Type)                          \
  static std::atomic<std::size_t> counter;                      \
  Type() { LOG(INFO) << __func__ << " " << ++counter; }         \
  ~Type() { LOG(INFO) << __func__ << " " << --counter; }
#else
#define DEFINE_ENGINE_DEBUG_INFO(Type)
#endif

// Forward declarations
struct ThreadedOpr;

/*!
 * \brief Operation block in the scheduler.
 *  Each OprBlock corresponds to an operation pushed to the engine.
 */
struct OprBlock : public common::ObjectPoolAllocatable<OprBlock> {
  /*!
   * \brief wait number of pending tasks this OprBlock is waiting for.
   */
  std::atomic<std::size_t> wait{0};
  /*! \brief Pointer to information on performing real operation */
  ThreadedOpr* opr{nullptr};
  /*! \brief The context this operator */
  Context ctx;
  // define possible debug information
  DEFINE_ENGINE_DEBUG_INFO(OprBlock);
};  // struct OprBlock

/*!
 * \brief VersionedVarBlock that corresponding to a variable version.
 *  This is a basic unit of LinkedList in the ThreadedVar.
 */
struct VersionedVarBlock
    : public common::ObjectPoolAllocatable<VersionedVarBlock> {
  /*! \brief next block in the LinkedList */
  VersionedVarBlock* next{nullptr};
  /*! \brief the operation this block triggers */
  OprBlock* trigger{nullptr};
  /*! \brief whether this operation is a write(mutate) operation. */
  bool write{false};
  /*! \brief define possible debug information */
  DEFINE_ENGINE_DEBUG_INFO(VersionedVarBlock);
};  // struct VersionedVarBlock

/*!
 * \brief Variable implementation.
 *  Each ThreadedVar is a linked list(queue) of operations to be performed.
 */
class ThreadedVar final : public Var,
                          public common::ObjectPoolAllocatable<ThreadedVar> {
 public:
  /*!
   * \brief constructor
   * \param head head block of the LinkedList,
   *             need to be initialized with next==nullptr and trigger=nullptr.
   */
  explicit ThreadedVar(VersionedVarBlock* head);
  /*!
   * \brief Schedule a read operation on this variable.
   *  If the opr_block can be runed right away,
   *  the wait counter of opr_block will be decreased.
   *  Otherwise, the opr_block will be added to waiting queue.
   * \param opr_block The operation to be scheduled.
   */
  void AppendReadDependency(OprBlock* opr_block);
  /*!
   * \brief Schedule a write operation on this variable.
   *  If the opr_block can be runed right away,
   *  the wait counter of opr_block will be decreased.
   *  Otherwise, the opr_block will be added to waiting queue.
   * \param opr_block The operation to be scheduled.
   */
  void AppendWriteDependency(OprBlock* opr_block);
  /*!
   * \brief A read operation is completed on this variable.
   *  This function may trigger subsequent waiting operations on this variable.
   *
   * \param dispatcher the function called to trigger the operation,
   *            when all of its dependencies are satiesfied.
   * \tparam Dispatcher the function called to trigger an operation.
   */
  template <typename Dispatcher>
  void CompleteReadDependency(Dispatcher dispatcher);
  /*!
   * \brief A write operation is completed on this variable.
   *  This function may trigger subsequent waiting operations on this variable.
   *
   * \param dispatcher the function called to trigger the operation,
   *            when all of its dependencies are satiesfied.
   * \tparam Dispatcher the function called to trigger an operation.
   * \return to_delete, whether this Variable can be deleted after this functin.
   */
  template <typename Dispatcher>
  bool CompleteWriteDependency(Dispatcher dispatcher);
  /*! \brief Mark this variable to be deleted. */
  void SetToDelete();
  /*! \return whether this variable is ready to read. */
  bool ready_to_read();
  /*!
   * \brief Cast a Var pointer to ThreadedVar pointer
   * \param ptr pointer from base.
   * \return a casted pointer.
   */
  inline static ThreadedVar* CastFromBase(Var* ptr) {
    return ptr->Cast<ThreadedVar>();
  }
  // code for debug.
#if ENGINE_DEBUG
  static std::atomic<std::size_t> counter;
  ~ThreadedVar() { LOG(INFO) << __func__ << " " << --counter; }
#endif  // ENGINE_DEBUG

 private:
  // TODO(hotpxl) change this to spinlock for faster runtime
  // TODO(hotpxl) consider rename head
  /*! \brief inetrnal mutex of the ThreadedVar */
  std::mutex m_;
  /*! \brief number of pending reads operation in the variable. */
  std::size_t num_pending_reads_{0};
  /*!
   * \brief Points to the last VersionedVarBlock in the queue.
   *  head_ always points to a empty VersionedVarBlock.
   *  So when we want to append an operation to the queue:
   *    1) update head_->trigger to be new op
   *    2) update head_->next to be a new VersionedVarBlock
   *    3) move head to head->next.
   */
  VersionedVarBlock* head_{nullptr};
  /*!
   * \brief The pointer to next write to perform.
   *  This pointer will only be updated when the write completes.
   *  This is actually the head(oldest operation) in the queue.
   */
  VersionedVarBlock* pending_write_{nullptr};
  /*!
   * \brief If true, then there are no running or pending write on this variable.
   */
  bool ready_to_read_{true};
  /*!
   * \brief If true, delete after operation completes.
   */
  bool to_delete_{false};
};  // struct ThreadedVar

/*!
 * \brief Operator used in ThreadedEngine.
 */
struct ThreadedOpr final : public Opr,
                           public common::ObjectPoolAllocatable<ThreadedOpr> {
  /*! \brief The function to be invoked each time. */
  Engine::AsyncFn fn;
  /*! \brief The variable this operation will read from. */
  std::vector<ThreadedVar*> const_vars;
  /*! \brief The variable this operation will mutate. */
  std::vector<ThreadedVar*> mutable_vars;
  /*! \brief the property of the operator */
  FnProperty prop;
  /*!
   * \brief Whether this is an temporary operator
   *        that can be deleted right after the operation completed.
   */
  bool temporary{false};
  /*!
   * \brief Cast a Opr pointer to ThreadedOpr pointer
   * \param ptr pointer from base.
   * \return a casted pointer.
   */
  inline static ThreadedOpr* CastFromBase(Opr* ptr) {
    return ptr->Cast<ThreadedOpr>();
  }
  // define possible debug information
  DEFINE_ENGINE_DEBUG_INFO(ThreadedOpr);
};  // struct ThreadedOpr

/*!
 * \brief Base class of all ThreadedEngine.
 *  This class implements a thread safe version of engine.
 *  The engine tracks the dependencies, and will call PushToExecute
 *  to execute a specific task.
 *
 *  Subclass can implement PushToExecute to design specific
 *  execution policy for the tasks.
 */
class ThreadedEngine : public Engine {
 public:
  // constructor
  ThreadedEngine() : pending_(0) {}
  // implementing all the functions from Engine.
  ThreadedVar* NewVariable() override;
  ThreadedOpr* NewOperator(AsyncFn fn,
                           std::vector<VarHandle> const& const_vars,
                           std::vector<VarHandle> const& mutable_vars,
                           FnProperty prop) override;
  void DeleteOperator(OprHandle op) override;
  void Push(OprHandle op, Context exec_ctx) override;
  void PushAsync(AsyncFn exec_fun, Context exec_ctx,
                 std::vector<VarHandle> const& const_vars,
                 std::vector<VarHandle> const& mutable_vars,
                 FnProperty prop) override;
  void DeleteVariable(SyncFn delete_fn, Context exec_ctx, VarHandle var) override;
  void WaitForVar(VarHandle var) override;
  void WaitForAll() override;

 protected:
  /*!
   * \brief Push the opr block to execution queue to be executed.
   *  This function is implemented by the corresponding subclass
   *  for specific policy.
   *
   * \param opr_block The operator block.
   * \param pusher_thread whether the caller is the thread that calls push
   */
  virtual void PushToExecute(OprBlock* opr_block, bool pusher_thread) = 0;
  /*!
   * \brief Call this function to actually execute an opr_block
   *  This function also deletes the opr_block after execution.
   * \param run_ctx runtime context used to execute the function.
   * \param opr_block the opr_block to be executed and deleted.
   */
  void ExecuteOprBlock(RunContext run_ctx, OprBlock *opr_block) {
    ThreadedOpr* threaded_opr = opr_block->opr;
    CallbackOnComplete callback = this->CreateCallback(
        ThreadedEngine::OnCompleteStatic, threaded_opr);
    threaded_opr->fn(run_ctx, callback);
    OprBlock::Delete(opr_block);
  }

 private:
  /*!
   * \brief check if thee is duplication in const_vars and mutable_vars.
   * \param const_vars the variables to read from.
   * \param mutable_vars the variables to mutate.
   */
  void CheckDuplicate(std::vector<VarHandle> const& const_vars,
                      std::vector<VarHandle> const& mutable_vars);
  /*!
   * \brief Callback on operation completion.
   *
   * On operation completion, this will trigger subsequent operations.
   */
  inline void OnComplete(ThreadedOpr* threaded_opr);
  // callback to the threaded engine
  static void OnCompleteStatic(Engine *engine, void *threaded_opr);
  /*!
   * \brief Number of pending operations.
   */
  std::atomic<std::size_t> pending_;
  /*!
   * \brief Mutex and condition_variable,
   *  used to Notify waits for single or all variables.
   */
  std::mutex finished_m_;
  std::condition_variable finished_cv_;
  /*!
   * \brief Disallow copy construction and assignment.
   */
  DISALLOW_COPY_AND_ASSIGN(ThreadedEngine);
};  // class ThreadedEngine

}  // namespace engine
}  // namespace mxnet
#endif  // MXNET_ENGINE_THREADED_ENGINE_H_
