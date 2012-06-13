require 'rubygems'
require 'ffi-rzmq'

class ZnowflakeMessage
  class Layout < FFI::Struct
    layout :b0, :uint8,
           :b1, :uint8,
           :b2, :uint8,
           :b3, :uint8,
           :b4, :uint8,
           :b5, :uint8,
           :b6, :uint8,
           :b7, :uint8
  end

  def initialize msg_struct = nil
    if msg_struct
      @msg_t = msg_struct
      @data = Layout.new @msg_t.data
    else
      @pointer = FFI::MemoryPointer.new :byte, Layout.size, true
      @data = Layout.new @pointer
    end
  end
  
  def size
    @size = @msg_t.size
  end
  
  def id
    # Convert from network byte order
    (@data[:b0] << 56) | (@data[:b1] << 48) | (@data[:b2] << 40) | (@data[:b3] << 32) |
      (@data[:b4] << 24) | (@data[:b5] << 16) | (@data[:b6] << 8) | @data[:b7]
  end
end

# Pull apart and print the ID
@bit_len = {
  :time    => 39,
  :machine => 15,
  :seq     => 10
}

@bit_shift = {
  :time    => @bit_len[:seq] + @bit_len[:machine],
  :machine => @bit_len[:seq]
}

@bit_mask = {
  :machine => (1 << @bit_len[:machine]) - 1,
  :seq     => (1 << @bit_len[:seq]) - 1
}

@epoch = 1337000000

def print_id id
  ts      = (@epoch * 1000) + (id >> @bit_shift[:time])
  sec     = ts / 1000
  msec    = ts - (sec * 1000)
  machine = (id >> @bit_shift[:machine]) & @bit_mask[:machine]
  seq     = id & @bit_mask[:seq]

  puts "id:          #{id}"
  puts "machine:     #{machine}"
  puts "datetime:    #{Time.at sec}"
  puts "timestamp:   #{sec}"
  puts "(msec, seq): (#{msec}, #{seq})"
  puts
end

# Grab some IDs off the socket
@port = 23138

context = ZMQ::Context.new
socket  = context.socket ZMQ::REQ
socket.connect "tcp://*:#{@port}"

100.times do
  socket.send_string ''

  message = ZMQ::Message.new
  successful_read = socket.recv message
  message = ZnowflakeMessage.new message if successful_read

  print_id message.id
end
