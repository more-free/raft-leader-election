# raft-leader-election
a C++ 11,  cross-platform, header-only Raft-based leader election library. 

### Leader Election
[Raft](https://raft.github.io/) is one of the most influential recent research result in distributed system area. It provides an easy-to-implement consensus algorithm which is the core of the critical building blocks of some modern distributed systems.

The Raft algorithm consists of two major parts, the leader election and the log replication. Most existing Raft implementations expose APIs in terms of "log replication", hiding leader election as an implementation details.  

However, the leader election part itself can be versatile. In a lot distributed systems, ex. GFS/HDFS, BigTable/HBase, Kafka, etc., leader election always plays an important role. Further, the leader election algorithms can be adapted to solve a wide range of problems, such as distributed lock. 

Though there are a wide range of leader election algorithms, the Raft paper presents one of the most robust and reliable algorithm. This project aims providing the Raft leader election part as a library.

### Usage 
[Here](https://github.com/more-free/raft-leader-election/blob/master/examples/leader_election.cpp) provides a basic example of using this library. Here is the illustration : 
```code
        // we start by setting up the cluster configurations.
        RaftOptions options;
        
        // create the id -> address mapping.
        // each peer (a server, a process, a thread...) must have a statically-assigned unique id.
        options.peers = {
            { 1, RaftFactory::create_remote_address(EndPoint("localhost", 9990)) },
            { 2, RaftFactory::create_remote_address(EndPoint("localhost", 9991)) },
            { 3, RaftFactory::create_remote_address(EndPoint("localhost", 9992)) },
            { 4, RaftFactory::create_remote_address(EndPoint("localhost", 9993)) },
            { 5, RaftFactory::create_remote_address(EndPoint("localhost", 9994)) },
        };
        
        // set the id of the this peer.
        options.self = 1;
        
        // provide a callback invoked right after this peer is elected as leader.
        options.on_become_leader = [](RaftFSM& fsm) { 
            cout << fsm.get_actor()->self_address()->to_string() << " was elected as leader" << endl; 
        };
        
        // RaftFSM is the main abstraction of the Raft algorithm, we call fsm->start() to start leader election
        // and trigger the callbacks,  and call fsm->stop() when we are done (which is optionally, it will be called
        // from RaftFSM's destructor).
        shared_ptr<RaftFSM> fsm = RaftFactory::create_raft_fsm(options);
        fsm->start();
```

The above code with slight modification can be adapted to provide a __distributed lock__. 
```code
        // called when the current server is elected as leader. it's semantically equivalent to a "lock" call.
        options.on_become_leader = [](RaftFSM& fsm) { 
            cout << fsm.get_actor()->self_address()->to_string() << " acquired lock" << endl;

            // sign-off the leadership. it's semantically equivalent to a "unlock" call. 
            fsm.stay_follower(); 
        };

        // optionally callback. called after the above "unlock" call finishes.
        options.on_sign_off_leader = [](RaftFSM& fsm) {
            cout << fsm.get_actor()->self_address()->to_string() << " released lock" << endl;
        };
```
See full example [here](https://github.com/more-free/raft-leader-election/blob/master/examples/distributed_lock.cpp).

Note that in the original Raft algorithm, it's possible that two leaders co-exist (but in different terms).  For example, consider node 1 - 5,  say node 1 is the original leader, and all of a sudden its network is partitioned and it loses connections to all other nodes. Note that in the case, node 1 won't convert back to follower on its own. Meanwhile, the other four nodes will elect a new leader (say it's node 2).  So globally there are two leaders 1 and 2, while node 1 has older term.
Also note that it's impossible to have three leaders in the same time. it's at most two.

In the semantics of exclusive lock, this is not allowed because this may indicate that two nodes (leaders) can access the shared resource at the same time.  

We can fix the problem by slightly modifying the original Raft algorithm as follows, while sacrificing a bit availability.

__Modification for exclusive distributed lock__ : 
a leader must sign-off the old leader before it claims itself a new leader. 

Applying the modification to the example above, that will be : before node 2 become a new leader (after it receives enough votes), it must send a special message _SignOffLeader_ to the old leader (node 1).  Only after node 2 receives confirmation from node 1, it could claim itself as a new leader.  In implementation, we add an extra _old_leader_ to the response of _RequestVote_ call, that's how node 2 knows the existence of the old leader node 1 (this is guaranteed because node 2 must receive the response from the majority of the nodes -- say it receives votes from 3 and 4. In (2, 3, 4) at least one of them knows the existence of old leader 1).

By doing this, we ensure that at any time, globally there is at most one leader alive, and that gives us a reliable distributed lock algorithm, with an extra availability requirement that node 2 must be able to talk to node 1.



