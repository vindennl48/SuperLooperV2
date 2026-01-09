#ifndef MINI_BUFFER_H
#define MINI_BUFFER_H

#include <Arduino.h>
#include <AudioStream.h>

/**
 * A simple templated ring buffer for storing Audio Library blocks.
 * Maintains ownership of the blocks it holds (calls release() on clear/destruct).
 */
template <size_t Capacity>
class MiniBuffer {
public:
  MiniBuffer() : head(0), tail(0), count(0) {
    // Initialize pointers to null for safety
    memset(buffer, 0, sizeof(buffer));
  }

  ~MiniBuffer() {
    clear();
  }

  /**
   * Adds an audio block to the buffer.
   * If buffer is full, the oldest block is released to make room.
   * NOTE: The buffer takes ownership of the block.
   */
  void push(audio_block_t* block) {
    if (count >= Capacity) {
        audio_block_t* oldBlock = pop();
        if (oldBlock) AudioStream::release(oldBlock);
    }

    buffer[head] = block;
    head = (head + 1) % Capacity;
    count++;
  }

  /**
   * Removes and returns the oldest audio block.
   * Returns nullptr if buffer is empty.
   * NOTE: Caller takes ownership of the block and is responsible for release() or transmit().
   */
  audio_block_t* pop() {
    if (count == 0) return nullptr;

    audio_block_t* block = buffer[tail];
    buffer[tail] = nullptr; // Safety
    tail = (tail + 1) % Capacity;
    count--;
    return block;
  }

  /**
   * Returns the oldest audio block without removing it.
   */
  audio_block_t* peek() {
    if (count == 0) return nullptr;
    return buffer[tail];
  }

  /**
   * Releases all held blocks back to the memory pool.
   */
  void clear() {
    while (count > 0) {
      audio_block_t* block = pop();
      if (block) AudioStream::release(block);
    }
    // pop() resets count, tail, and buffer entries
    head = 0;
    tail = 0;
  }

  bool isFull() const { return count >= Capacity; }
  bool isEmpty() const { return count == 0; }
  size_t size() const { return count; }

private:
  audio_block_t* buffer[Capacity];
  size_t head;
  size_t tail;
  size_t count;
};

#endif
