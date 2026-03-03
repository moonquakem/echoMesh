#pragma once

#include "message.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <optional>
#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>
#include <future>
#include <vector>
#include <atomic>

using UserId = int64_t;
using RoomId = std::string;

// A type alias for the bidirectional audio stream
using AudioStream = grpc::ServerReaderWriter<echomesh::VoicePacket, echomesh::VoicePacket>;

class ThreadPool;

// Safe wrapper for AudioStream to prevent concurrent writes and use-after-free.
class StreamWrapper : public std::enable_shared_from_this<StreamWrapper> {
public:
    StreamWrapper(AudioStream* stream) : stream_(stream), closed_(false), is_draining_(false) {}
    
    bool enqueue(const echomesh::VoicePacket& packet, ThreadPool& pool);
    
    // Updated close to wait for active drainers
    void close() {
        std::unique_lock<std::mutex> lock(mutex_);
        closed_ = true;
        
        // Wait until any active drain task finishes its current Write and exits
        // This ensures the stream_ pointer isn't used after this function returns
        close_cv_.wait(lock, [this] { return !is_draining_; });
        
        stream_ = nullptr;
        std::queue<echomesh::VoicePacket> empty;
        std::swap(write_queue_, empty);
    }

private:
    void drain();

    AudioStream* stream_;
    bool closed_;
    std::queue<echomesh::VoicePacket> write_queue_;
    std::mutex mutex_;
    std::condition_variable close_cv_;
    std::atomic<bool> is_draining_;
};

// A simple thread pool for handling asynchronous tasks
class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i<threads; ++i)
            workers.emplace_back([this] {
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }
    
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop) return;
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

class Room {
public:
  void addUser(UserId userId);
  void removeUser(UserId userId);
  std::set<UserId> getUsers() const;

  // Stream management
  void addAudioStream(UserId userId, AudioStream* stream);
  void removeAudioStream(UserId userId);
  void broadcastAudio(UserId senderId, const echomesh::VoicePacket& packet, ThreadPool& pool);

private:
  mutable std::mutex mutex_;
  std::set<UserId> users_;
  std::unordered_map<UserId, std::shared_ptr<StreamWrapper>> audio_streams_;
};

class RoomManager {
public:
  static RoomManager &getInstance();

  bool createRoom(const RoomId &roomId);
  bool joinRoom(const RoomId &roomId, UserId userId);
  void leaveRoom(const RoomId &roomId, UserId userId);
  void userLogout(UserId userId);
  std::shared_ptr<Room> getRoom(const RoomId &roomId);
  std::set<UserId> getUsersInRoom(const RoomId &roomId);

  void addAudioStream(const RoomId& roomId, UserId userId, AudioStream* stream);
  void removeAudioStream(const RoomId& roomId, UserId userId);
  void broadcastAudio(const RoomId& roomId, UserId senderId, const echomesh::VoicePacket& packet);


private:
  RoomManager();
  ~RoomManager() = default;
  RoomManager(const RoomManager &) = delete;
  RoomManager &operator=(const RoomManager &) = delete;

  bool createRoom_nl(const RoomId &roomId);

  mutable std::mutex mutex_;
  std::unordered_map<RoomId, std::shared_ptr<Room>> rooms_;
  std::unordered_map<UserId, RoomId> user_to_room_map_;
  
  std::unique_ptr<ThreadPool> m_threadPool;
};
