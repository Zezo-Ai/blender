/* SPDX-FileCopyrightText: 2006 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bli
 */

#include <algorithm>
#include <cerrno>
#include <cstdio> /* For `printf`. */
#include <cstdlib>
#include <deque>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_threads.h"
#include "BLI_time.h"
#include "BLI_utildefines.h"

/* for checking system threads - BLI_system_thread_count */
#ifdef WIN32
#  include <sys/timeb.h>
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#else
#  include <sys/time.h>
#  include <unistd.h>
#endif

#ifdef WITH_TBB
#  include <tbb/spin_mutex.h>
#endif

#include "atomic_ops.h"

/**
 * Basic Thread Control API
 * ========================
 *
 * Many thread cases have an X amount of jobs, and only an Y amount of
 * threads are useful (typically amount of CPU's)
 *
 * This code can be used to start a maximum amount of 'thread slots', which
 * then can be filled in a loop with an idle timer.
 *
 * A sample loop can look like this (pseudo c);
 *
 * \code{.c}
 *
 *   ListBase lb;
 *   int max_threads = 2;
 *   int cont = 1;
 *
 *   BLI_threadpool_init(&lb, do_something_func, max_threads);
 *
 *   while (cont) {
 *     if (BLI_available_threads(&lb) && !(escape loop event)) {
 *       // get new job (data pointer)
 *       // tag job 'processed
 *       BLI_threadpool_insert(&lb, job);
 *     }
 *     else BLI_time_sleep_ms(50);
 *
 *     // Find if a job is ready, this the do_something_func() should write in job somewhere.
 *     cont = 0;
 *     for (go over all jobs)
 *       if (job is ready) {
 *         if (job was not removed) {
 *           BLI_threadpool_remove(&lb, job);
 *         }
 *       }
 *       else cont = 1;
 *     }
 *     // Conditions to exit loop.
 *     if (if escape loop event) {
 *       if (BLI_available_threadslots(&lb) == max_threads) {
 *         break;
 *       }
 *     }
 *   }
 *
 *   BLI_threadpool_end(&lb);
 *
 * \endcode
 */
static pthread_mutex_t _image_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _image_draw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _viewer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _custom1_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _nodes_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _movieclip_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _colormanage_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _fftw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _view3d_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mainid;
static uint thread_levels = 0; /* threads can be invoked inside threads */
static int threads_override_num = 0;

/* just a max for security reasons */
#define RE_MAX_THREAD BLENDER_MAX_THREADS

struct ThreadSlot {
  ThreadSlot *next, *prev;
  void *(*do_thread)(void *);
  void *callerdata;
  pthread_t pthread;
  int avail;
};

void BLI_threadapi_init()
{
  mainid = pthread_self();
}

void BLI_threadapi_exit() {}

void BLI_threadpool_init(ListBase *threadbase, void *(*do_thread)(void *), int tot)
{
  int a;

  if (threadbase != nullptr && tot > 0) {
    BLI_listbase_clear(threadbase);

    if (tot > RE_MAX_THREAD) {
      tot = RE_MAX_THREAD;
    }
    else if (tot < 1) {
      tot = 1;
    }

    for (a = 0; a < tot; a++) {
      ThreadSlot *tslot = MEM_callocN<ThreadSlot>("threadslot");
      BLI_addtail(threadbase, tslot);
      tslot->do_thread = do_thread;
      tslot->avail = 1;
    }
  }

  atomic_fetch_and_add_u(&thread_levels, 1);
}

int BLI_available_threads(ListBase *threadbase)
{
  int counter = 0;

  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail) {
      counter++;
    }
  }

  return counter;
}

int BLI_threadpool_available_thread_index(ListBase *threadbase)
{
  int counter = 0;

  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail) {
      return counter;
    }
    ++counter;
  }

  return 0;
}

static void *tslot_thread_start(void *tslot_p)
{
  ThreadSlot *tslot = (ThreadSlot *)tslot_p;
  return tslot->do_thread(tslot->callerdata);
}

int BLI_thread_is_main()
{
  return pthread_equal(pthread_self(), mainid);
}

void BLI_threadpool_insert(ListBase *threadbase, void *callerdata)
{
  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail) {
      tslot->avail = 0;
      tslot->callerdata = callerdata;
      pthread_create(&tslot->pthread, nullptr, tslot_thread_start, tslot);
      return;
    }
  }
  printf("ERROR: could not insert thread slot\n");
}

void BLI_threadpool_remove(ListBase *threadbase, void *callerdata)
{
  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->callerdata == callerdata) {
      pthread_join(tslot->pthread, nullptr);
      tslot->callerdata = nullptr;
      tslot->avail = 1;
    }
  }
}

void BLI_threadpool_remove_index(ListBase *threadbase, int index)
{
  int counter = 0;

  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (counter == index && tslot->avail == 0) {
      pthread_join(tslot->pthread, nullptr);
      tslot->callerdata = nullptr;
      tslot->avail = 1;
      break;
    }
    ++counter;
  }
}

void BLI_threadpool_clear(ListBase *threadbase)
{
  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail == 0) {
      pthread_join(tslot->pthread, nullptr);
      tslot->callerdata = nullptr;
      tslot->avail = 1;
    }
  }
}

void BLI_threadpool_end(ListBase *threadbase)
{

  /* Only needed if there's actually some stuff to end
   * this way we don't end up decrementing thread_levels on an empty `threadbase`. */
  if (threadbase == nullptr || BLI_listbase_is_empty(threadbase)) {
    return;
  }

  LISTBASE_FOREACH (ThreadSlot *, tslot, threadbase) {
    if (tslot->avail == 0) {
      pthread_join(tslot->pthread, nullptr);
    }
  }
  BLI_freelistN(threadbase);
}

/* System Information */

int BLI_system_thread_count()
{
  static int t = -1;

  if (threads_override_num != 0) {
    return threads_override_num;
  }
  if (LIKELY(t != -1)) {
    return t;
  }

  {
#ifdef WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    t = int(info.dwNumberOfProcessors);
#else
#  ifdef __APPLE__
    int mib[2];
    size_t len;

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    len = sizeof(t);
    sysctl(mib, 2, &t, &len, nullptr, 0);
#  else
    t = int(sysconf(_SC_NPROCESSORS_ONLN));
#  endif
#endif
  }

  CLAMP(t, 1, RE_MAX_THREAD);

  return t;
}

void BLI_system_num_threads_override_set(int num)
{
  threads_override_num = num;
}

int BLI_system_num_threads_override_get()
{
  return threads_override_num;
}

/* Global Mutex Locks */

static ThreadMutex *global_mutex_from_type(const int type)
{
  switch (type) {
    case LOCK_IMAGE:
      return &_image_lock;
    case LOCK_DRAW_IMAGE:
      return &_image_draw_lock;
    case LOCK_VIEWER:
      return &_viewer_lock;
    case LOCK_CUSTOM1:
      return &_custom1_lock;
    case LOCK_NODES:
      return &_nodes_lock;
    case LOCK_MOVIECLIP:
      return &_movieclip_lock;
    case LOCK_COLORMANAGE:
      return &_colormanage_lock;
    case LOCK_FFTW:
      return &_fftw_lock;
    case LOCK_VIEW3D:
      return &_view3d_lock;
    default:
      BLI_assert_unreachable();
      return nullptr;
  }
}

void BLI_thread_lock(int type)
{
  pthread_mutex_lock(global_mutex_from_type(type));
}

void BLI_thread_unlock(int type)
{
  pthread_mutex_unlock(global_mutex_from_type(type));
}

/* Mutex Locks */

void BLI_mutex_init(ThreadMutex *mutex)
{
  pthread_mutex_init(mutex, nullptr);
}

void BLI_mutex_lock(ThreadMutex *mutex)
{
  pthread_mutex_lock(mutex);
}

void BLI_mutex_unlock(ThreadMutex *mutex)
{
  pthread_mutex_unlock(mutex);
}

bool BLI_mutex_trylock(ThreadMutex *mutex)
{
  return (pthread_mutex_trylock(mutex) == 0);
}

void BLI_mutex_end(ThreadMutex *mutex)
{
  pthread_mutex_destroy(mutex);
}

ThreadMutex *BLI_mutex_alloc()
{
  ThreadMutex *mutex = MEM_callocN<ThreadMutex>("ThreadMutex");
  BLI_mutex_init(mutex);
  return mutex;
}

void BLI_mutex_free(ThreadMutex *mutex)
{
  BLI_mutex_end(mutex);
  MEM_freeN(mutex);
}

/* Spin Locks */

#ifdef WITH_TBB
static tbb::spin_mutex *tbb_spin_mutex_cast(SpinLock *spin)
{
  static_assert(sizeof(SpinLock) >= sizeof(tbb::spin_mutex),
                "SpinLock must match tbb::spin_mutex");
  static_assert(alignof(SpinLock) % alignof(tbb::spin_mutex) == 0,
                "SpinLock must be aligned same as tbb::spin_mutex");
  return reinterpret_cast<tbb::spin_mutex *>(spin);
}
#endif

void BLI_spin_init(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  new (spin_mutex) tbb::spin_mutex();
#elif defined(__APPLE__)
  BLI_mutex_init(spin);
#elif defined(_MSC_VER)
  *spin = 0;
#else
  pthread_spin_init(spin, 0);
#endif
}

void BLI_spin_lock(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  spin_mutex->lock();
#elif defined(__APPLE__)
  BLI_mutex_lock(spin);
#elif defined(_MSC_VER)
#  if defined(_M_ARM64)
  // InterlockedExchangeAcquire takes a long arg on MSVC ARM64
  static_assert(sizeof(long) == sizeof(SpinLock));
  while (InterlockedExchangeAcquire((volatile long *)spin, 1)) {
#  else
  while (InterlockedExchangeAcquire(spin, 1)) {
#  endif
    while (*spin) {
      /* Spin-lock hint for processors with hyper-threading. */
      YieldProcessor();
    }
  }
#else
  pthread_spin_lock(spin);
#endif
}

void BLI_spin_unlock(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  spin_mutex->unlock();
#elif defined(__APPLE__)
  BLI_mutex_unlock(spin);
#elif defined(_MSC_VER)
  _ReadWriteBarrier();
  *spin = 0;
#else
  pthread_spin_unlock(spin);
#endif
}

void BLI_spin_end(SpinLock *spin)
{
#ifdef WITH_TBB
  tbb::spin_mutex *spin_mutex = tbb_spin_mutex_cast(spin);
  spin_mutex->~spin_mutex();
#elif defined(__APPLE__)
  BLI_mutex_end(spin);
#elif defined(_MSC_VER)
  /* Nothing to do, spin is a simple integer type. */
  UNUSED_VARS(spin);
#else
  pthread_spin_destroy(spin);
#endif
}

/* Read/Write Mutex Lock */

void BLI_rw_mutex_init(ThreadRWMutex *mutex)
{
  pthread_rwlock_init(mutex, nullptr);
}

void BLI_rw_mutex_lock(ThreadRWMutex *mutex, int mode)
{
  if (mode == THREAD_LOCK_READ) {
    pthread_rwlock_rdlock(mutex);
  }
  else {
    pthread_rwlock_wrlock(mutex);
  }
}

void BLI_rw_mutex_unlock(ThreadRWMutex *mutex)
{
  pthread_rwlock_unlock(mutex);
}

void BLI_rw_mutex_end(ThreadRWMutex *mutex)
{
  pthread_rwlock_destroy(mutex);
}

ThreadRWMutex *BLI_rw_mutex_alloc()
{
  ThreadRWMutex *mutex = MEM_callocN<ThreadRWMutex>("ThreadRWMutex");
  BLI_rw_mutex_init(mutex);
  return mutex;
}

void BLI_rw_mutex_free(ThreadRWMutex *mutex)
{
  BLI_rw_mutex_end(mutex);
  MEM_freeN(mutex);
}

/* Ticket Mutex Lock */

struct TicketMutex {
  pthread_cond_t cond;
  pthread_mutex_t mutex;
  uint queue_head, queue_tail;
  pthread_t owner;
  bool has_owner;
};

TicketMutex *BLI_ticket_mutex_alloc()
{
  TicketMutex *ticket = MEM_callocN<TicketMutex>("TicketMutex");

  pthread_cond_init(&ticket->cond, nullptr);
  pthread_mutex_init(&ticket->mutex, nullptr);

  return ticket;
}

void BLI_ticket_mutex_free(TicketMutex *ticket)
{
  pthread_mutex_destroy(&ticket->mutex);
  pthread_cond_destroy(&ticket->cond);
  MEM_freeN(ticket);
}

static bool ticket_mutex_lock(TicketMutex *ticket, const bool check_recursive)
{
  uint queue_me;

  pthread_mutex_lock(&ticket->mutex);

  /* Check for recursive locks, for debugging only. */
  if (check_recursive && ticket->has_owner && pthread_equal(pthread_self(), ticket->owner)) {
    pthread_mutex_unlock(&ticket->mutex);
    return false;
  }

  queue_me = ticket->queue_tail++;

  while (queue_me != ticket->queue_head) {
    pthread_cond_wait(&ticket->cond, &ticket->mutex);
  }

  ticket->owner = pthread_self();
  ticket->has_owner = true;

  pthread_mutex_unlock(&ticket->mutex);
  return true;
}

void BLI_ticket_mutex_lock(TicketMutex *ticket)
{
  ticket_mutex_lock(ticket, false);
}

bool BLI_ticket_mutex_lock_check_recursive(TicketMutex *ticket)
{
  return ticket_mutex_lock(ticket, true);
}

void BLI_ticket_mutex_unlock(TicketMutex *ticket)
{
  pthread_mutex_lock(&ticket->mutex);
  ticket->queue_head++;
  ticket->has_owner = false;
  pthread_cond_broadcast(&ticket->cond);
  pthread_mutex_unlock(&ticket->mutex);
}

/* ************************************************ */

/* Condition */

void BLI_condition_init(ThreadCondition *cond)
{
  pthread_cond_init(cond, nullptr);
}

void BLI_condition_wait(ThreadCondition *cond, ThreadMutex *mutex)
{
  pthread_cond_wait(cond, mutex);
}

void BLI_condition_wait_global_mutex(ThreadCondition *cond, const int type)
{
  pthread_cond_wait(cond, global_mutex_from_type(type));
}

void BLI_condition_notify_one(ThreadCondition *cond)
{
  pthread_cond_signal(cond);
}

void BLI_condition_notify_all(ThreadCondition *cond)
{
  pthread_cond_broadcast(cond);
}

void BLI_condition_end(ThreadCondition *cond)
{
  pthread_cond_destroy(cond);
}

/* ************************************************ */

struct ThreadQueueWork {
  void *work;
  uint64_t id;
};

struct ThreadQueue {
  uint64_t current_id = 0;
  std::deque<ThreadQueueWork> queue_low_priority;
  std::deque<ThreadQueueWork> queue_normal_priority;
  std::deque<ThreadQueueWork> queue_high_priority;
  pthread_mutex_t mutex;
  pthread_cond_t push_cond;
  pthread_cond_t finish_cond;
  volatile int nowait = 0;
  volatile int canceled = 0;
};

ThreadQueue *BLI_thread_queue_init()
{
  ThreadQueue *queue = MEM_new<ThreadQueue>(__func__);

  pthread_mutex_init(&queue->mutex, nullptr);
  pthread_cond_init(&queue->push_cond, nullptr);
  pthread_cond_init(&queue->finish_cond, nullptr);

  return queue;
}

void BLI_thread_queue_free(ThreadQueue *queue)
{
  /* destroy everything, assumes no one is using queue anymore */
  pthread_cond_destroy(&queue->finish_cond);
  pthread_cond_destroy(&queue->push_cond);
  pthread_mutex_destroy(&queue->mutex);

  MEM_delete(queue);
}

uint64_t BLI_thread_queue_push(ThreadQueue *queue, void *work, ThreadQueueWorkPriority priority)
{
  BLI_assert(work);

  pthread_mutex_lock(&queue->mutex);

  ThreadQueueWork work_reference;
  work_reference.work = work;
  work_reference.id = ++queue->current_id;

  switch (priority) {
    case BLI_THREAD_QUEUE_WORK_PRIORITY_LOW:
      queue->queue_low_priority.push_back(work_reference);
      break;
    case BLI_THREAD_QUEUE_WORK_PRIORITY_NORMAL:
      queue->queue_normal_priority.push_back(work_reference);
      break;
    case BLI_THREAD_QUEUE_WORK_PRIORITY_HIGH:
      queue->queue_high_priority.push_back(work_reference);
      break;
  }

  /* signal threads waiting to pop */
  pthread_cond_signal(&queue->push_cond);
  pthread_mutex_unlock(&queue->mutex);

  return work_reference.id;
}

/** WARNING: Assumes the queue is already locked. */
static void check_finalization(ThreadQueue *queue)
{
  if (queue->queue_low_priority.empty() && queue->queue_normal_priority.empty() &&
      queue->queue_high_priority.empty())
  {
    pthread_cond_signal(&queue->finish_cond);
  }
}

void BLI_thread_queue_cancel_work(ThreadQueue *queue, uint64_t work_id)
{
  pthread_mutex_lock(&queue->mutex);

  bool found = false;
  auto check = [&](const ThreadQueueWork &work) {
    if (work.id == work_id) {
      found = true;
      return true;
    }
    return false;
  };

  auto cancel = [&](std::deque<ThreadQueueWork> &sub_queue) {
    sub_queue.erase(std::remove_if(sub_queue.begin(), sub_queue.end(), check), sub_queue.end());
  };

  cancel(queue->queue_low_priority);
  cancel(queue->queue_normal_priority);
  cancel(queue->queue_high_priority);

  if (found) {
    check_finalization(queue);
  }

  pthread_mutex_unlock(&queue->mutex);
}

void *BLI_thread_queue_pop(ThreadQueue *queue)
{
  ThreadQueueWork work_reference = {0};

  /* wait until there is work */
  pthread_mutex_lock(&queue->mutex);
  while (!queue->nowait && queue->queue_low_priority.empty() &&
         queue->queue_normal_priority.empty() && queue->queue_high_priority.empty())
  {
    pthread_cond_wait(&queue->push_cond, &queue->mutex);
  }

  /* if we have something, pop it */
  for (std::deque<ThreadQueueWork> *sub_queue :
       {&queue->queue_high_priority, &queue->queue_normal_priority, &queue->queue_low_priority})
  {
    if (sub_queue->empty()) {
      continue;
    }
    work_reference = sub_queue->front();
    sub_queue->pop_front();

    /* Don't pop more than one work. */
    break;
  }

  if (work_reference.work) {
    check_finalization(queue);
  }

  pthread_mutex_unlock(&queue->mutex);

  return work_reference.work;
}

static void wait_timeout(timespec *timeout, int ms)
{
  ldiv_t div_result;
  long sec, usec, x;

#ifdef WIN32
  {
    struct _timeb now;
    _ftime(&now);
    sec = now.time;
    usec = now.millitm * 1000; /* microsecond precision would be better */
  }
#else
  {
    timeval now;
    gettimeofday(&now, nullptr);
    sec = now.tv_sec;
    usec = now.tv_usec;
  }
#endif

  /* add current time + millisecond offset */
  div_result = ldiv(ms, 1000);
  timeout->tv_sec = sec + div_result.quot;

  x = usec + (div_result.rem * 1000);

  if (x >= 1000000) {
    timeout->tv_sec++;
    x -= 1000000;
  }

  timeout->tv_nsec = x * 1000;
}

void *BLI_thread_queue_pop_timeout(ThreadQueue *queue, int ms)
{
  double t;
  ThreadQueueWork work_reference = {0};
  timespec timeout;

  t = BLI_time_now_seconds();
  wait_timeout(&timeout, ms);

  /* wait until there is work */
  pthread_mutex_lock(&queue->mutex);
  while (!queue->nowait && queue->queue_low_priority.empty() &&
         queue->queue_normal_priority.empty() && queue->queue_high_priority.empty())
  {
    if (pthread_cond_timedwait(&queue->push_cond, &queue->mutex, &timeout) == ETIMEDOUT) {
      break;
    }
    if (BLI_time_now_seconds() - t >= ms * 0.001) {
      break;
    }
  }

  /* if we have something, pop it */
  for (std::deque<ThreadQueueWork> *sub_queue :
       {&queue->queue_high_priority, &queue->queue_normal_priority, &queue->queue_low_priority})
  {
    if (sub_queue->empty()) {
      continue;
    }
    work_reference = sub_queue->front();
    sub_queue->pop_front();

    /* Don't pop more than one work. */
    break;
  }

  if (work_reference.work) {
    check_finalization(queue);
  }

  pthread_mutex_unlock(&queue->mutex);

  return work_reference.work;
}

int BLI_thread_queue_len(ThreadQueue *queue)
{
  int size;

  pthread_mutex_lock(&queue->mutex);
  size = queue->queue_low_priority.size() + queue->queue_normal_priority.size() +
         queue->queue_high_priority.size();
  pthread_mutex_unlock(&queue->mutex);

  return size;
}

bool BLI_thread_queue_is_empty(ThreadQueue *queue)
{
  bool is_empty;

  pthread_mutex_lock(&queue->mutex);
  is_empty = queue->queue_low_priority.empty() && queue->queue_normal_priority.empty() &&
             queue->queue_high_priority.empty();
  pthread_mutex_unlock(&queue->mutex);

  return is_empty;
}

void BLI_thread_queue_nowait(ThreadQueue *queue)
{
  pthread_mutex_lock(&queue->mutex);

  queue->nowait = 1;

  /* signal threads waiting to pop */
  pthread_cond_broadcast(&queue->push_cond);
  pthread_mutex_unlock(&queue->mutex);
}

void BLI_thread_queue_wait_finish(ThreadQueue *queue)
{
  /* wait for finish condition */
  pthread_mutex_lock(&queue->mutex);

  while (!queue->queue_low_priority.empty() || !queue->queue_normal_priority.empty() ||
         !queue->queue_high_priority.empty())
  {
    pthread_cond_wait(&queue->finish_cond, &queue->mutex);
  }

  pthread_mutex_unlock(&queue->mutex);
}
