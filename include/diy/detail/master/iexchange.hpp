namespace diy
{
    struct Master::IExchangeInfo
    {
        using   Clock   = std::chrono::high_resolution_clock;
        using   Time    = Clock::time_point;

        struct Message
        {
            std::array<int,2>   msg;
            mpi::request        request;
        };

                        IExchangeInfo():
                            n(0),
                            min_queue_size_(0),
                            max_hold_time_(0),
                            gid(-1),
                            fine_(false)                                          {}
                        IExchangeInfo(size_t n_, mpi::communicator comm_, size_t min_queue_size, size_t max_hold_time, bool fine):
                            n(n_),
                            comm(comm_),
                            min_queue_size_(min_queue_size),
                            max_hold_time_(max_hold_time),
                            gid(-1),
                            fine_(fine)                                           { time_stamp_send(); }

      inline void       not_done(int gid);
      inline void       update_done(int gid, bool done_);

      inline bool       all_done();                             // get global all done status
      inline void       add_work(int work);                     // add work to global work counter
      inline void       update_subtree(int diff);
      inline void       control();
      inline bool       process_work_update();
      inline void       check_for_abort();
      inline void       abort(int trial);
      inline void       process_done(int source, int trial);
      inline void       reset_child_confirmations();
      int               right_bit(int x) const                  { return ((x ^ (x-1)) + 1) >> 1; }

      void              send(int rk, int  type, int  x)
      {
          inflight_.emplace_back();
          Message& m = inflight_.back();
          m.msg[0] = type;
          m.msg[1] = x;
          m.request = comm.issend(rk, tags::iexchange, m.msg);
          log->trace("[{}] Sending to {}, type = {}, x = {}", comm.rank(), rk, type, x);
      }
      void              recv(int rk, int& type, int& x)
      {
          std::array<int,2> msg;
          comm.recv(rk, tags::iexchange, msg);
          type = msg[0];
          x = msg[1];
          log->trace("[{}] Received from {}, type = {}, x = {}, msg = {}", comm.rank(), rk, type, x);
      }

      inline bool       nudge();
      int               parent() const                          { return comm.rank() & (comm.rank() - 1); }     // flip the last 1 to 0
      inline void       signal_children(int tag, int x);
      bool              incomplete() const                      { return subtree_work_ > 0 || !inflight_.empty(); }
      bool              stale() const                           { return subtree_work_ != last_subtree_work_message_ || local_work_ != last_local_work_message_; }

      void              inc_work()                              { add_work(1); }   // increment work counter
      void              dec_work()                              { add_work(-1); }  // decremnent work counter

      void              time_stamp_send()                       { time_last_send = Clock::now(); }
      bool              hold(size_t queue_size)                 { return min_queue_size_ >= 0 && max_hold_time_ >= 0 &&
                                                                         queue_size < min_queue_size_ && hold_time() < max_hold_time_; }
      size_t            hold_time()                             { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - time_last_send).count(); }
      bool              shortcut()                              { return gid >= 0; }
      void              clear_shortcut()                        { gid = -1; }
      void              set_shortcut(int gid_,
                                     BlockID to_block)          { gid = gid_; block_id = to_block; }


      bool              fine() const                            { return fine_; }

      struct type       { enum {
                                    work_update = 0,
                                    done,
                                    abort
                                }; };
      size_t                              n;
      mpi::communicator                   comm;
      std::unordered_map<int, bool>       done;                 // gid -> done

      int                                 local_work_   = 0, last_local_work_message_   = 0;
      int                                 subtree_work_ = 0, last_subtree_work_message_ = 0;
      int                                 down_up_down_ = 0;

      std::list<Message>                  inflight_;
      int                                 last_trial_ = -1;
      int                                 child_confirmations = -1;

      std::shared_ptr<spd::logger>        log = get_logger();
      Time                                time_last_send;       // time of last send from any queue in send_outgoing_queues()

      // TODO: for now, negative min_queue_size or max_hold_time indicates don't do fine-grain icommunicate at all
      int                                 min_queue_size_;      // minimum short message size (bytes)
      int                                 max_hold_time_;       // maximum short message hold time (milliseconds)

      // the following 2 members are for short-cutting send_outgoing_queues
      // gid = -1 means ignore gid and block_id and don't shortcut send_outgoing_queues
      int                                 gid;                  // gid of most recent enqueue
      BlockID                             block_id;             // block id of target of most recent enqueue

      bool                                fine_ = false;

      // debug
      double                              dud_start_time;
    };
}

void
diy::Master::IExchangeInfo::
not_done(int gid)
{
    if (down_up_down_ > 0)
        log->info("Marking {} not done, current state = {}", gid, done[gid]);

    update_done(gid, false);
}

void
diy::Master::IExchangeInfo::
update_done(int gid, bool done_)
{
    if (done[gid] != done_)
    {
        done[gid] = done_;
        if (done_)
        {
            dec_work();
            log->debug("[{}] Decrementing work when switching done after callback, for {}\n", comm.rank(), gid);
        }
        else
        {
            inc_work();
            log->debug("[{}] Incrementing work when switching done after callback, for {}\n", comm.rank(), gid);
        }
    }
}

// get global all done status
bool
diy::Master::IExchangeInfo::
all_done()
{
    if (down_up_down_ == 3)
        while (!inflight_.empty()) nudge();     // make sure that all the messages are propagated before we finish
                                                // if we've decided that we are done, the only outstanding messages
                                                // can be the done signals to children; nothing else should be in-flight

    return down_up_down_ == 3;
}

// add arbitrary units of work to global work counter
void
diy::Master::IExchangeInfo::
add_work(int work)
{
    int cur_local_work = local_work_;
    local_work_ += work;
    assert(local_work_ >= 0);

    log->trace("[{}] Adding work: work = {}, local_work = {}, cur_local_work = {}", comm.rank(), work, local_work_, cur_local_work);

    if ((cur_local_work == 0) ^ (local_work_ == 0))     // changed from or to zero
    {
        int diff    = (local_work_ - last_local_work_message_);
        update_subtree(diff);
        last_local_work_message_ = local_work_;
    }
}

void
diy::Master::IExchangeInfo::
update_subtree(int diff)
{
    int cur_subtree_work = subtree_work_;
    subtree_work_ += diff;
    log->debug("[{}] Updating subtree: diff = {}, subtree_work_ = {}", comm.rank(), diff, subtree_work_);
    assert(subtree_work_ >= 0);

    if ((cur_subtree_work == 0) ^ (subtree_work_ == 0))     // changed from or to zero
    {
        if (comm.rank() != 0)
        {
            int subtree_diff = (subtree_work_ - last_subtree_work_message_);
            log->debug("[{}] Sending subtree update: diff = {}, subtree_diff = {}", comm.rank(), diff, subtree_diff);
            send(parent(), type::work_update, subtree_diff);
            last_subtree_work_message_ = subtree_work_;
            if (down_up_down_ == 1)
                abort(last_trial_);
            else if (down_up_down_ == 2)
                log->warn("[{}] Enqueueing work update after finishing, diff = {}", comm.rank(), subtree_diff);
                // This is Ok in general: if this happens, somebody else must abort this trial.
            else if (down_up_down_ == 3)
                log->critical("[{}] Enqueueing work update after all done, diff = {}", comm.rank(), subtree_diff);
        } else
        {
            assert(down_up_down_ < 2);
            down_up_down_ = 0;      // if we are updating work on the root, definitely abort the down-up-down protocol
        }
    }
}

void
diy::Master::IExchangeInfo::
control()
{
    mpi::optional<mpi::status> ostatus = comm.iprobe(mpi::any_source, tags::iexchange);
    while(ostatus)
    {
        int t, x;
        recv(ostatus->source(), t, x);
        if (t == type::work_update)
        {
            // x = diff
            log->debug("[{}] subtree update request from {}, diff = {}", comm.rank(), ostatus->source(), x);
            update_subtree(x);      // for now propagates up the tree verbatim
        } else if (t == type::abort)
        {
            // x = trial
            assert(x >= -1);
            abort(x);
        } else if (t == type::done)
        {
            process_done(ostatus->source(), x);
        }
        ostatus = comm.iprobe(mpi::any_source, tags::iexchange);
    }

    // initiate down-up-down protocol
    if (subtree_work_ == 0 && comm.rank() == 0 && down_up_down_ == 0)
    {
        // debug
        dud_start_time = MPI_Wtime();

        down_up_down_ = 1;
        reset_child_confirmations();
        if (child_confirmations)
        {
            signal_children(type::done, ++last_trial_);
            log->info("Initiated down-up-down, trial = {}", last_trial_);
        } else // no children
            down_up_down_ = 3;
    }

    while(nudge());
}

void
diy::Master::IExchangeInfo::
abort(int trial)
{
    if (down_up_down_ == 0) // already aborted
        return;

    if (trial != last_trial_)
        return;

    log->warn("[{}] aborting trial {}", comm.rank(), trial);
    assert(trial >= 0);

    down_up_down_ = 0;

    if (comm.rank() != 0)
    {
        send(parent(), type::abort, trial);    // propagate abort
        if (down_up_down_ >= 2)
            log->critical("[{}] sending abort after done", comm.rank());
        last_trial_ = -1;       // all future aborts for this trial will be stale
    }
}

void
diy::Master::IExchangeInfo::
process_done(int source, int trial)
{
    if (trial < -1)
    {
        log->critical("[{}] done with source = {}, trial = {}", comm.rank(), source, trial);
        assert(trial >= -1);
    }

    while(nudge());     // clear up finished requests: this is necessary since requests may have been received;
                        // we are now getting responses to them; but they are still listed in our inflight_

    if (source == parent())
    {
        if (trial == last_trial_)       // confirmation that we are done
        {
            assert(down_up_down_ == 2);
            log->info("[{}] received done confirmation from parent, trial = {}; incomplete = {}, subtree = {}, stale = {}",
                      comm.rank(), trial, incomplete(), subtree_work_, stale());
            down_up_down_ = 3;
            assert(!incomplete() && !stale());
        } else
        {
            last_trial_ = trial;
            down_up_down_ = 1;

            // check that there are no changes
            if (incomplete() || stale())
                abort(trial);
        }

        // pass the message to children
        if (down_up_down_ > 0)
        {
            reset_child_confirmations();
            if (child_confirmations)
            {
                log->info("[{}] signalling done to children, trial = {}", comm.rank(), trial);
                signal_children(type::done, trial);
            }
            else if (down_up_down_ < 2)     // no children, signal back to parent right away, unless it was the final done
            {
                down_up_down_ = 2;
                log->info("[{}] signalling done to parent (1), trial = {}, incomplete = {}", comm.rank(), trial, incomplete());
                send(parent(), type::done, trial);
            }
        }
    } else // signal from a child
    {
        if (trial == last_trial_)
        {
            int child_mask = right_bit(source);
            child_confirmations &= ~child_mask;
            if (child_confirmations == 0)       // done
            {
                if (comm.rank() != 0)
                {
                    if (incomplete() || stale())        // heard from all the children, but there is something incomplete
                        abort(trial);
                    else
                    {
                        down_up_down_ = 2;
                        log->info("[{}] signalling done to parent (2), trial = {}, incomplete = {}", comm.rank(), trial, incomplete());
                        send(parent(), type::done, trial);
                    }
                }
                else if (down_up_down_ == 1)
                {
                    log->info("[{}] received done confirmation from children at root, trial = {}", comm.rank(), trial);
                    // initiate final down
                    down_up_down_ = 3;
                    signal_children(type::done, trial);
                }
            }
        } // else stale trial confirmation, drop
    }
}

void
diy::Master::IExchangeInfo::
reset_child_confirmations()
{
    child_confirmations = 0;
    int child_mask = 1;
    int child = child_mask | comm.rank();
    while (child != comm.rank() && child < comm.size())
    {
        child_confirmations |= child_mask;

        child_mask <<= 1;
        child = child_mask | comm.rank();
    }
}

void
diy::Master::IExchangeInfo::
signal_children(int tag, int x)
{
    int child_mask = 1;
    int child = child_mask | comm.rank();
    while (child != comm.rank() && child < comm.size())
    {
        send(child, tag, x);

        child_mask <<= 1;
        child = child_mask | comm.rank();
    }
}

bool
diy::Master::IExchangeInfo::
nudge()
{
  bool success = false;
  for (auto it = inflight_.begin(); it != inflight_.end();)
  {
    mpi::optional<mpi::status> ostatus = it->request.test();
    if (ostatus)
    {
      success = true;
      it = inflight_.erase(it);
    }
    else
      ++it;
  }
  return success;
}


