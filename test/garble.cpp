#include <iostream>

#include "emp-tool/emp-tool.h"
using namespace std;
using namespace emp;

class AbandonIO: public IOChannel<AbandonIO> { public:
	void send_data_internal(const void * data, int len) {
	}

	void recv_data_internal(void  * data, int len) {
	}
};

int port, party;
template <typename T>
void test(T* netio) {
	block* a = new block[128];
	block* b = new block[128];
	block* c = new block[128];

	PRG prg;
	prg.random_block(a, 128);
	prg.random_block(b, 128);

	string file = "./emp-tool/circuits/files/bristol_format/AES-non-expanded.txt";
	BristolFormat cf(file.c_str());

	if (party == BOB) {
		HalfGateEva<T>::circ_exec = new HalfGateEva<T>(netio);
		for (int i = 0; i < 100; ++i)
			cf.compute(c, a, b);
		delete HalfGateEva<T>::circ_exec;
	} else {
		AbandonIO* aio = new AbandonIO();
		HalfGateGen<AbandonIO>::circ_exec = new HalfGateGen<AbandonIO>(aio);

		auto start = clock_start();
		for (int i = 0; i < 100; ++i) {
			cf.compute(c, a, b);
		}
		double interval = time_from(start);
		cout << "Pure AES garbling speed : " << 100 * 6800 / interval << " million gate per second\n";
		delete aio;
		delete HalfGateGen<AbandonIO>::circ_exec;

		MemIO* mio = new MemIO();
		HalfGateGen<MemIO>::circ_exec = new HalfGateGen<MemIO>(mio);

		start = clock_start();
		for (int i = 0; i < 20; ++i) {
			mio->clear();
			for (int j = 0; j < 5; ++j)
				cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Writing to Memory : " << 100 * 6800 / interval << " million gate per second\n";
		delete mio;
		delete HalfGateGen<MemIO>::circ_exec;

		HalfGateGen<T>::circ_exec = new HalfGateGen<T>(netio);

		start = clock_start();
		for (int i = 0; i < 100; ++i) {
			cf.compute(c, a, b);
		}
		interval = time_from(start);
		cout << "AES garbling + Loopback Network : " << 100 * 6800 / interval << " million gate per second\n";

		delete HalfGateGen<T>::circ_exec;
	}

	delete[] a;
	delete[] b;
	delete[] c;
}

void test_mem(NetIO *netio) {
	block* a = new block[6];
	block* b = new block[6];
	block* c = new block[1];                                                    

	PRG prg;
	prg.random_block(a, 6);
	prg.random_block(b, 6);

	string file = "./emp-tool/circuits/files/bristol_format/adder_msb_6bit.txt";
	BristolFormat cf(file.c_str());
	int garbled_table_size = 208;

	if (party == BOB) {
		block na[6], nb[6];
		uint8_t garbled_table[garbled_table_size];
		netio->recv_data(garbled_table, garbled_table_size);
		netio->recv_block(na, 6);
		netio->recv_block(nb, 6);
		std::cout << "================= Bob MemIO Info =================" << std::endl;
		for (int i = 0; i < garbled_table_size; i++) {
			printf("%02x ", garbled_table[i]);
			if ((i + 1) % 16 == 0) std::cout << std::endl;
		}
		// new Bob MemIO
		MemIO *mio = new MemIO();
		mio->size = garbled_table_size;
		mio->cap = 1024 * 1024;
		mio->read_pos = 0;
		memcpy(mio->buffer, garbled_table, garbled_table_size);
		block delta;
		HalfGateEva<MemIO>::circ_exec = new HalfGateEva<MemIO>(mio);
		for (int i = 0; i < 6; i++) {
			std::cout << "na: " << na[i] << ",nb: " << nb[i] << std::endl;
		}
		cf.compute(c, na, nb);
		std::cout << "res: " << c[0] << std::endl;

	} else {
		MemIO* mio = new MemIO();
		block delta;
		prg.random_block(&delta);
		std::cout << "custom delta is " << delta << std::endl;
  	HalfGateGen<MemIO>::circ_exec = new HalfGateGen<MemIO>(mio, delta);
		cf.compute(c, a, b);

		uint8_t tmp[garbled_table_size];
		assert(garbled_table_size == mio->size);
		memcpy(tmp, (uint8_t *)mio->buffer, garbled_table_size);
		std::cout << "================= Alice MemIO Info =================" << std::endl;
		std::cout << "=== Buffer size: " << mio->size << std::endl;
		std::cout << "=== Cap size: " << mio->cap << std::endl;
		std::cout << "=== Detailed Data: " << std::endl;
		for (int i = 0; i < garbled_table_size; i++) {
			printf("%02x ", tmp[i]);
			if ((i + 1) % 16 == 0) std::cout << std::endl;
		}
		std::cout << "==============================================" << std::endl;
		std::cout << c[0] << " " << (c[0] ^ delta) << std::endl;

		//generate some data
		uint8_t ra[6] = {0, 1, 1, 1, 1};
		uint8_t rb[6] = {0, 1, 0, 1, 0};
		block na[6], nb[6];
		for (int i = 0; i < 6; i++) {
			na[i] = ra[i] ? a[i] ^ delta : a[i];
			nb[i] = rb[i] ? b[i] ^ delta : b[i];
		}

		for (int i = 0; i < 6; i++) {
			std::cout << "na:" << na[i] << ",nb: " << nb[i] << std::endl;
		}
		netio->send_data(tmp, garbled_table_size);
		netio->send_block(na, 6);
		netio->send_block(nb, 6);


		delete mio;
		delete HalfGateGen<MemIO>::circ_exec;
	}
	delete[] a;
	delete[] b;
	delete[] c;
}

int main(int argc, char** argv) {
	parse_party_and_port(argv, &party, &port);
	cout << "Using NetIO\n";
	NetIO* netio = new NetIO(party == ALICE ? nullptr : "127.0.0.1", port);
	// test<NetIO>(netio);
	// delete netio;

	// cout << "Using HighSpeedNetIO\n";
	// HighSpeedNetIO* hsnetio = new HighSpeedNetIO(party == ALICE ? nullptr : "127.0.0.1", port, port+1);
	// test<HighSpeedNetIO>(hsnetio);
	// delete hsnetio;
	test_mem(netio);
	delete netio;
}
