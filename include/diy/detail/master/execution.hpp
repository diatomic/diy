#include <algorithm>

struct diy::Master::ProcessBlock
{
          ProcessBlock(Master&                    master_,
                       const std::deque<int>&     blocks__,
                       int                        local_limit_,
                       critical_resource<int>&    idx_):
              master(master_),
              blocks(blocks__),
              local_limit(local_limit_),
              idx(idx_)
          {}

          ProcessBlock(const ProcessBlock&)     = delete;
          ProcessBlock(ProcessBlock&&)          = default;

  void    operator()()
  {
    // master.log->debug("Processing with thread: {}",  this_thread::get_id());

    std::vector<int>      local;
    do
    {
      int cur = (*idx.access())++;

      if ((size_t)cur >= blocks.size())
          return;

      int i   = blocks[cur];
      int gid = master.gid(i);
      stats::Annotation::Guard g( stats::Annotation("diy.block").set(gid) );

      if (master.block(i))
      {
          if (local.size() == (size_t)local_limit)
              master.unload(local);
          local.push_back(i);
      }

      master.log->debug("Processing block: {}", gid);

      bool skip = all_skip(i);

      IncomingQueuesMap &current_incoming = master.incoming_[master.exchange_round_].map;

      if (master.block(i) == 0)             // block unloaded
      {
          if (skip)
              master.load_queues(i);        // even though we are skipping the block, the queues might be necessary
          else
          {
              if (local.size() == (size_t)local_limit)      // reached the local limit
                  master.unload(local);

              master.load(i);
              local.push_back(i);
          }
      }

      for (auto& cmd : master.commands_)
      {
          cmd->execute(skip ? 0 : master.block(i), master.proxy(i));

          // no longer need them, so get rid of them
          current_incoming[gid].clear();
      }

      if (skip && master.block(i) == 0)
          master.unload_queues(i);    // even though we are skipping the block, the queues might be necessary
    } while(true);
  }

  bool  all_skip(int i) const
  {
      bool skip = true;
      for (auto& cmd : master.commands_)
      {
          if (!cmd->skip(i, master))
          {
              skip = false;
              break;
          }
      }
      return skip;
  }

  Master&                 master;
  const std::deque<int>&  blocks;
  int                     local_limit;
  critical_resource<int>& idx;
};

struct diy::Master::DynamicProcessBlock
{
          DynamicProcessBlock(Master&                             master_,
                       const std::deque<int>&                     blocks__,
                       int                                        local_limit_,
                       critical_resource<int>&                    idx_,
                       critical_resource<std::vector<int>>&       free_blocks_):
              master(master_),
              blocks(blocks__),
              local_limit(local_limit_),
              idx(idx_),
              free_blocks(free_blocks_)
          {}

          DynamicProcessBlock(const DynamicProcessBlock&)     = delete;
          DynamicProcessBlock(DynamicProcessBlock&&)          = default;

  void    operator()()
  {
    master.log->debug("Processing with thread: {}",  this_thread::get_id());

    std::vector<int>      local;
    do
    {
      int cur = (*idx.access())++;

      if ((size_t)cur >= blocks.size())
          return;

      int i   = blocks[cur];
      int gid = master.gid(i);
      int lid = master.lid(gid);

      // lock the block
      (*free_blocks.access())[lid] = -1;

      stats::Annotation::Guard g( stats::Annotation("diy.block").set(gid) );

      if (master.block(i))
      {
          if (local.size() == (size_t)local_limit)
              master.unload(local);
          local.push_back(i);
      }

      master.log->debug("Processing block: {}", gid);

      bool skip = all_skip(i);

      IncomingQueuesMap &current_incoming = master.incoming_[master.exchange_round_].map;

      if (master.block(i) == 0)             // block unloaded
      {
          if (skip)
              master.load_queues(i);        // even though we are skipping the block, the queues might be necessary
          else
          {
              if (local.size() == (size_t)local_limit)      // reached the local limit
                  master.unload(local);

              master.load(i);
              local.push_back(i);
          }
      }

      for (auto& cmd : master.commands_)
      {
          cmd->execute(skip ? 0 : master.block(i), master.proxy(i));

          // no longer need them, so get rid of them
          current_incoming[gid].clear();
      }

      if (skip && master.block(i) == 0)
          master.unload_queues(i);    // even though we are skipping the block, the queues might be necessary

      // unlock the block
      (*free_blocks.access())[lid] = lid;

    } while(true);
  }

  bool  all_skip(int i) const
  {
      bool skip = true;
      for (auto& cmd : master.commands_)
      {
          if (!cmd->skip(i, master))
          {
              skip = false;
              break;
          }
      }
      return skip;
  }

  Master&                                 master;
  const std::deque<int>&                  blocks;
  int                                     local_limit;
  critical_resource<int>&                 idx;
  critical_resource<std::vector<int>>&    free_blocks;
};

void
diy::Master::
execute()
{
  log->debug("Entered execute()");
  auto scoped = prof.scoped("execute");
  DIY_UNUSED(scoped);
  //show_incoming_records();

  // touch the outgoing and incoming queues as well as collectives to make sure they exist
  for (unsigned i = 0; i < size(); ++i)
  {
    outgoing(gid(i));
    incoming(gid(i));           // implicitly touches queue records
    collectives(gid(i));
  }

  if (commands_.empty())
      return;

  // Order the blocks, so the loaded ones come first
  std::deque<int>   blocks;
  for (unsigned i = 0; i < size(); ++i)
    if (block(i) == 0)
        blocks.push_back(i);
    else
        blocks.push_front(i);

  // don't use more threads than we can have blocks in memory
  int num_threads;
  int blocks_per_thread;
  if (limit_ == -1)
  {
    num_threads = threads_;
    blocks_per_thread = size();
  }
  else
  {
    num_threads = (std::min)(threads_, limit_);
    blocks_per_thread = limit_/num_threads;
  }

  // idx is shared
  critical_resource<int> idx(0);

  if (num_threads > 1)
  {
    // launch the threads
    std::list<thread>   threads;
    for (unsigned i = 0; i < (unsigned)num_threads; ++i)
        threads.emplace_back(ProcessBlock(*this, blocks, blocks_per_thread, idx));

    for (auto& t : threads)
        t.join();
  } else
  {
      ProcessBlock(*this, blocks, blocks_per_thread, idx)();
  }

  // clear incoming queues
  incoming_[exchange_round_].map.clear();

  if (limit() != -1 && in_memory() > limit())
      throw std::runtime_error(fmt::format("Fatal: {} blocks in memory, with limit {}", in_memory(), limit()));

  // clear commands
  commands_.clear();
}

void
diy::Master::
dynamic_execute(critical_resource<std::vector<int>>& free_blocks)
{
  log->debug("Entered execute()");
  auto scoped = prof.scoped("execute");
  DIY_UNUSED(scoped);
  //show_incoming_records();

  // touch the outgoing and incoming queues as well as collectives to make sure they exist
  for (unsigned i = 0; i < size(); ++i)
  {
    // skip blocks that are not free
    if ((*free_blocks.access())[i] == -1)
        continue;
    outgoing(gid(i));
    incoming(gid(i));           // implicitly touches queue records
    collectives(gid(i));
  }

  if (commands_.empty())
      return;

  // Order the blocks, so the loaded ones come first
  std::deque<int>   blocks;
  for (unsigned i = 0; i < size(); ++i)
  {
    // skip blocks that are not free
    if ((*free_blocks.access())[i] == -1)
        continue;
    if (block(i) == 0)
        blocks.push_back(i);
    else
        blocks.push_front(i);
  }

  // don't use more threads than we can have blocks in memory
  int num_threads;
  int blocks_per_thread;
  if (limit_ == -1)
  {
    num_threads = threads_;
    blocks_per_thread = size();
  }
  else
  {
    num_threads = (std::min)(threads_, limit_);
    blocks_per_thread = limit_/num_threads;
  }

  // idx is shared
  critical_resource<int> idx(0);

  if (num_threads > 1)
  {
    // launch the threads
    std::list<thread>   threads;
    for (unsigned i = 0; i < (unsigned)num_threads; ++i)
        threads.emplace_back(ProcessBlock(*this, blocks, blocks_per_thread, idx));

    for (auto& t : threads)
        t.join();
  } else
  {
      DynamicProcessBlock(*this, blocks, blocks_per_thread, idx, free_blocks)();
  }

  // clear incoming queues
  incoming_[exchange_round_].map.clear();

  if (limit() != -1 && in_memory() > limit())
      throw std::runtime_error(fmt::format("Fatal: {} blocks in memory, with limit {}", in_memory(), limit()));

  // clear commands
  commands_.clear();
}
