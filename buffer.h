/**
 * File:    buffer.h
 * Author:  Karl Kangur <karl.kangur@gmail.com>
 * Licnece: GNU General Public License
 * URL:     https://github.com/Nurgak/IoT-RGB-LED-Matrix
 */

class Buffer: public Stream
{
private:
  uint16_t address;
  uint16_t size;
public:
  // Allow "direct memory access"
  uint8_t * data;
  Buffer(uint16_t);
  //bool operator==(Buffer compare);
  void begin();  
  void seek(uint16_t);
  size_t write(uint8_t);
  
  // Stream object functions
  int read();
  int available()
  {
    return -1;
  }
  void flush()
  {
  }
  int peek()
  {
    return -1;
  }
  using Print::write;
};

bool operator==(Buffer a, Buffer b)
{
  // Compare the addresses of the matrices to compare two matrics
  return &a == &b;
}

Buffer::Buffer(uint16_t size)
{
  this->address = 0;
  this->size = size;
}

void Buffer::begin()
{
  // Reserve the memory and initialise to 0
  this->data = new uint8_t[this->size]();
}

void Buffer::seek(uint16_t address)
{
  if(this->address < this->size)
  {
    this->address = address;
  }
}

size_t Buffer::write(uint8_t value)
{
  if(address >= this->size)
  {
    return 0;
  }
  *(this->data + this->address) = value;
  // Increment address for the next write
  this->address++;
  // Write one byte
  return 1;
}

int Buffer::read()
{
  if(this->address >= this->size)
  {
    return -1;
  }
  uint8_t data = this->data[this->address];
  // Increment address for the next read
  this->address++;
  return data;
}

