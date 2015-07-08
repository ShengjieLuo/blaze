package org.apache.spark.acc_runtime;

import java.lang.InterruptedException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.channels.FileChannel.MapMode;
import java.io.*;
import java.util.*;
import java.net.Socket;
import java.net.SocketException;
import java.lang.System;

/**
* The class is in charge of maintaining the connection between Spark and Accelerator manager.
* DataTransmitter is used for building the connection, send and receive the data between Spark
* and Accelerator manager.
**/
public class DataTransmitter {
	Socket acc_socket = null;

	public DataTransmitter() {
		init("127.0.0.1", 1027);
	}

	/**
	* Initialize connection.
	* Initialize the connection. This method should be called only once to avoid 
	* duplicated connections.
	*
	* @param hostname 
	*		The hostname of the Accelerator manager.
	* @param port
	*		The port of the Accelerator manager.
	**/
	public void init(String hostname, int port) {
		try {
      acc_socket = new Socket(hostname, port); 
    } catch (Exception e) {
      e.printStackTrace(System.err);
    }
	}

	/**
	* Send message to Accelerator manager.
	* This method also builds the connection when it is called first time by launching 
	* method init.
	*
	* @param msg
	*		The message to be sent.
	*	@see init(String hostname, int port)
	**/
	public void send(AccMessage.TaskMsg.Builder msgBuilder)
    throws IOException {

		AccMessage.TaskMsg msg = msgBuilder.build();
    int msg_size = msg.getSerializedSize();

    // send byte size
    DataOutputStream sout = new DataOutputStream(acc_socket.getOutputStream());
    sout.write(ByteBuffer.allocate(4)
        .order(ByteOrder.nativeOrder())
        .putInt(msg_size).array(),0,4);
    sout.flush();

    // send message data
    msg.writeTo(sout);
    sout.flush();
  }

	/**
	* Receive the message from Accelerator manager.
	**/
  public AccMessage.TaskMsg receive()
    throws IOException {

    DataInputStream sin = new DataInputStream(acc_socket.getInputStream());

    int msg_size = ByteBuffer.allocate(4)
      .putInt(sin.readInt())
      .order(ByteOrder.nativeOrder())
      .getInt(0);

    byte[] msg_data = ByteBuffer.allocate(msg_size).array();

    sin.read(msg_data, 0, msg_size);

    return AccMessage.TaskMsg.parseFrom(msg_data);
  }

	/**
	* Create a task message for requesting.
	**/
	public AccMessage.TaskMsg.Builder buildRequest(int pid, int[] blockId) {
		AccMessage.TaskMsg.Builder msg = AccMessage.TaskMsg.newBuilder()
			.setType(AccMessage.MsgType.ACCREQUEST)
			.setAccId("request" + pid);

			for (int id: blockId) {
				AccMessage.Data.Builder data = AccMessage.Data.newBuilder()
					.setPartitionId(id);
				msg.addData(data);
			}
		
		return msg;
	}

	/**
	* Create an empty data message.
	**/
	public AccMessage.TaskMsg.Builder buildData(int id) {
		AccMessage.TaskMsg.Builder msg = AccMessage.TaskMsg.newBuilder()
			.setType(AccMessage.MsgType.ACCDATA);

		return msg;
	}

	/**
	* Add a data block.
	**/
	public void addData(AccMessage.TaskMsg.Builder msg, int id, int width, int size, int offset, String path) {
		AccMessage.Data.Builder data = AccMessage.Data.newBuilder()
			.setPartitionId(id)
			.setWidth(width)
			.setSize(size)
			.setOffset(offset)
			.setPath(path);

		msg.addData(data);
		return ;
	}
}