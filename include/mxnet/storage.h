/*!
 * Copyright (c) 2015 by Contributors
 * \file storage.h
 * \brief Storage manager across multiple devices.
 */
#ifndef MXNET_STORAGE_H_
#define MXNET_STORAGE_H_

#include <memory>
#include "./base.h"

namespace mxnet {

/*!
 * \brief Storage manager across multiple devices.
 */
class Storage {
 public:
  /*!
   * \brief Storage handle.
   */
  struct Handle {
    /*!
     * \brief Pointer to the data.
     */
    void* dptr;
    /*!
     * \brief Size of the storage.
     */
    size_t size;
    /*!
     * \brief Context information about device and ID.
     */
    Context ctx;
  };
  /*!
   * \brief Allocate a new contiguous memory for a given size.
   * \param size Total size of memory in bytes.
   * \param ctx Context information about the device and ID.
   * \return Handle struct.
   */
  Handle Alloc(size_t size, Context ctx);
  /*!
   * \brief Free storage.
   * \param handle Handle struect.
   */
  void Free(Handle handle);
  /*!
   * \brief Destructor.
   */
  ~Storage();
  /*!
   * \return Storage singleton.
   */
  static Storage* Get();
  /*!
   * \brief Get shared pointer reference to engine singleton.
   *  Most user should not call this function.
   *  This function is called by another singleton X who requires
   *  Storage to be destructed after X.
   *
   * \return A shared pointer to Storage singleton.
   */
  static std::shared_ptr<Storage> _GetSharedRef();

 private:
  /*!
   * \brief Hidden constructors.
   */
  Storage();
  struct Impl;
  std::unique_ptr<Impl> impl_;
  DISALLOW_COPY_AND_ASSIGN(Storage);
};  // class Storage
}  // namespace mxnet
#endif  // MXNET_STORAGE_H_
