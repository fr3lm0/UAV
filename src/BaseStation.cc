#include <math.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <complex>
#include <getopt.h>
#include <liquid/liquid.h>

#include <liquid/ofdmtxrx.h>
#include "timer.h"

struct packet
{
	unsigned int id;
	unsigned char data[1024];
	timer send_timer;
	bool operator ==(const packet &rhs)
	{
		return id == rhs.id;
	}
};

std::list<packet> transmitted_packets;
std::list<unsigned int> retransmit_packets;

unsigned int received_acks = 0;
unsigned int received_nacks = 0;
unsigned int timeouts  = 0;

int callback(unsigned char *  _header,
		int              _header_valid,
		unsigned char *  _payload,
		unsigned int     _payload_len,
		int              _payload_valid,
		framesyncstats_s _stats,
		void *           _userdata)
{
	if(_header_valid)
	{
		unsigned int packet_type = _header[2];
		unsigned int rx_id = (_header[0] << 8 | _header[1]);
		if(packet_type == 0)
		{
			packet pk;
			pk.id = rx_id;
			transmitted_packets.remove(pk);
			received_acks++;
		}
		else if(packet_type == 1)
		{
			retransmit_packets.push_back(rx_id);
			received_nacks++;
		}
	}
	return 0;
}

void usage() {
	printf("Transmission options:\n");
	printf("  --tx-freq				Set the transmission frequency\n");
	printf("								[Default: 2.495 GHz]\n");
	printf("  --rx-freq				Set the reception frequency\n");
	printf("								[Default: 2.497 GHz]\n");
	printf("  --tx-sw-gain				Set the software transmit gain\n");
	printf("								[Default: -12 db]\n");
	printf("  --tx-hw-gain				Set the hardware transmit gain\n");
	printf("								[Default: 40 db]\n");
	printf("  --rx-hw-gain				Set the hardware reception gain\n");
	printf("								[Default: 20 db]\n");
	printf("  --mod-scheme				Set the modulation scheme to use for transmission\n");
	printf("								[Default: QPSK]\n");
	printf("  Available options:\n");
	liquid_print_modulation_schemes();
	printf("\n");
	printf("  --inner-fec				Set the inner FEC scheme\n");
	printf("								[Default: none]\n");
	printf("  --outer-fec				Set the outer FEC scheme\n");
	printf("								[Default: V29P23]\n");
	printf("  Available options:\n");
	liquid_print_fec_schemes();
	printf("\n");
	printf("OFDM options:\n");
	printf("  --num-subcarriers			Set the number of OFDM subcarriers\n");
	printf("								[Default: 48]\n");
	printf("  --cyclic-prefix-len			Set the OFDM cyclic prefix length\n");
	printf("								[Default: 6]\n");
	printf("  --taper-len				Set the OFDM taper length\n");
	printf("								[Default: 4]\n");
	printf("Miscellaneous options:\n");
	printf("  --num-packets				Set the number of packets to send to the UAV\n");
	printf("								[Default: 1000]\n");
	printf("  --payload-len				Set the size of each packet\n");
	printf("								[Default: 1024 bytes]\n");
	printf("  --retransmit-timeout			Set the time to wait before retransmitting packets\n");
	printf("								[Default: 1.0 seconds\n");
	printf("  --verbose				Enable extra output\n");
	printf("								[Default: false]\n");
	printf("  --help				Display this help message\n");
	exit(0);
}

int main (int argc, char **argv)
{
    // command-line options
    bool verbose = false;
    double frequency = 462e6;         // carrier frequency
    double bandwidth = 500e3f;         // bandwidth
	double frequency_separation = 4 * bandwidth;
	double tx_frequency = frequency;         // carrier frequency
	double rx_frequency = 464e6;       // carrier frequency
    unsigned int num_frames = 1000;     // number of frames to transmit
    double txgain_dB = -12.0f;          // software tx gain [dB]
    double uhd_txgain = 40.0;           // uhd (hardware) tx gain
	float uhd_rxgain = 20.0;

    // ofdm properties
    unsigned int M = 48;                // number of subcarriers
    unsigned int cp_len = 6;            // cyclic prefix length
    unsigned int taper_len = 4;         // taper length

    modulation_scheme ms = LIQUID_MODEM_BPSK;// modulation scheme
    unsigned int payload_len = 256;        // original data message length
    //crc_scheme check = LIQUID_CRC_32;       // data validity check
    fec_scheme fec0 = LIQUID_FEC_CONV_V29P23;      // fec (inner)
    fec_scheme fec1 = LIQUID_FEC_RS_M8; // fec (outer)
    
	float packet_timeout = 1.0;


    //
    int c;
    static struct option long_options[] = {
			{"tx-freq",				required_argument, 0, 'a'},
			{"rx-freq",				required_argument, 0, 'b'},
			{"tx-sw-gain",			required_argument, 0, 'c'},
			{"tx-hw-gain",			required_argument, 0, 'd'},
			{"rx-hw-gain",			required_argument, 0, 'e'},
			{"num-subcarriers",		required_argument, 0, 'f'},
			{"cyclic-prefixi-len",	required_argument, 0, 'g'},
			{"taper-len",			required_argument, 0, 'h'},
			{"num-packets",			required_argument, 0, 'i'},
			{"payload-len",			required_argument, 0, 'j'},
			{"mod-scheme",			required_argument, 0, 'k'},
			{"inner-fec",			required_argument, 0, 'l'},
			{"outer-fec",			required_argument, 0, 'm'},
			{"retransmit-timeout",	required_argument, 0, 'n'},
            {"help",                no_argument,       0, 'o'},
            {"verbosity",           required_argument, 0, 'p'},
    };
    int option_index = 0;
	
	while (1) 
	{
		c = getopt_long(argc, argv, "",
				long_options, &option_index);

		
		if (c == -1)
			break;
		switch (c) 
		{
			case 'a' :
				tx_frequency = atof(optarg);
				break;
			case 'b' :
				rx_frequency = atof(optarg);
				break;
			case 'c' :
				txgain_dB = atof(optarg);
				break;
			case 'd' :
				uhd_txgain = atof(optarg);
				break;
			case 'e' :
				uhd_rxgain = atof(optarg);
				break;
			case 'f' :
				M = atoi(optarg);
				break;
			case 'g' :
				cp_len = atoi(optarg);
				break;
			case 'h' :
				taper_len = atoi(optarg);
				break;
			case 'i' :
				num_frames = atoi(optarg);
				break;
			case 'j' :
				payload_len = atoi(optarg);
				break;
			case 'k' :
				ms = liquid_getopt_str2mod(optarg);
				if(ms == LIQUID_MODEM_UNKNOWN)
				{
					std::cout << "Modulation scheme " << ms << " not supported. Using QPSK." << std::endl;
					ms = LIQUID_MODEM_QPSK;
				}
				break;
			case 'l' :
				fec0 = liquid_getopt_str2fec(optarg);
				if(fec0 == LIQUID_FEC_UNKNOWN)
				{
					std::cout << "FEC scheme " << fec0 << " not supported. Using none." << std::endl;
					fec0 = LIQUID_FEC_NONE;
				}
				break;
			case 'm' :
				fec1 = liquid_getopt_str2fec(optarg);
				if(fec1 == LIQUID_FEC_UNKNOWN)
				{
					std::cout << "FEC scheme " << fec1 << " not supported. Using none." << std::endl;
					fec1 = LIQUID_FEC_NONE;
				}
				break;
			case 'n' :
				packet_timeout = atof(optarg);
				break;
			case 'o' :
				usage();
				break;
			case 'p' :
				verbose = true;
				break;
				
		}
	
	}


	std::cout << "tx freq: " << tx_frequency << std::endl;
	std::cout << "rx freq: " << rx_frequency << std::endl;



	if (cp_len == 0 || cp_len > M) {
        fprintf(stderr,"error: %s, cyclic prefix must be in (0,M]\n", argv[0]);
        exit(1);
    } else if (ms == LIQUID_MODEM_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported mod. scheme\n", argv[0]);
        exit(-1);
    } else if (fec0 == LIQUID_FEC_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported inner fec scheme\n", argv[0]);
        exit(-1);
    } else if (fec1 == LIQUID_FEC_UNKNOWN) {
        fprintf(stderr,"error: %s, unknown/unsupported outer fec scheme\n", argv[0]);
        exit(-1);
    }

    // create transceiver object
    unsigned char * p = NULL;   // default subcarrier allocation
    ofdmtxrx txcvr(M, cp_len, taper_len, p, callback, NULL);

    // set properties
    txcvr.set_tx_freq(tx_frequency);
    txcvr.set_tx_rate(bandwidth);
    txcvr.set_tx_gain_soft(txgain_dB);
    txcvr.set_tx_gain_uhd(uhd_txgain);

	txcvr.set_rx_freq(rx_frequency);
	txcvr.set_rx_rate(bandwidth);
	txcvr.set_rx_gain_uhd(uhd_rxgain);
	

	txcvr.start_rx();
    // data arrays
    unsigned char header[8];
    unsigned char payload[payload_len];
    
	timer print_timer = timer_create();
	timer_tic(print_timer);
	bool printing = false;
	unsigned int id;
    unsigned int pid;
    unsigned int i;
	packet pk;
    for (pid=0; pid<num_frames || transmitted_packets.size() > 0; pid++) 
	{
		std::list<packet>::iterator it;
		if(timer_toc(print_timer) > .1)
			printing = true;
		if(printing)std::cout << "packets still waiting for ack: ";
		for(it = transmitted_packets.begin(); it != transmitted_packets.end(); it++)
		{
			if(printing)std::cout << (*it).id << " ";
			if(timer_toc((*it).send_timer) > packet_timeout)
			{
				std::cout << "no response for packet " << (*it).id << std::endl;
				timer_tic((*it).send_timer);
				retransmit_packets.push_back((*it).id);
				timeouts++;
			}
		}
		if(printing)
		{
			std::cout << std::endl;
			timer_tic(print_timer);
		}
		printing = false;
		while(retransmit_packets.size() > 0)
		{
			id = retransmit_packets.front();
			retransmit_packets.pop_front();
			pk.id = id;
			std::list<packet>::iterator iter = std::find(transmitted_packets.begin(), transmitted_packets.end(), pk);
			if(iter != transmitted_packets.end())
			{
				pk = (*iter);
				header[0] = (id >> 8) & 0xff;
				header[1] = (id     ) & 0xff;
				for (i=2; i<8; i++)
					header[i] = rand() & 0xff;

				for (i=0; i<payload_len; i++)
					payload[i] = rand() & 0xff;

				std::cout << "retransmitting packet " << id << std::endl;
				txcvr.transmit_packet(header, pk.data, payload_len, ms, fec0, fec1);
			}
		}
		if(pid < num_frames)
		{
			if (verbose)
				printf("tx packet id: %6u\n", pid);

			// write header (first two bytes packet ID, remaining are random)
			header[0] = (pid >> 8) & 0xff;
			header[1] = (pid     ) & 0xff;
			for (i=2; i<8; i++)
				header[i] = rand() & 0xff;

			// initialize payload
			for (i=0; i<payload_len; i++)
				payload[i] = rand() & 0xff;
			pk.id = pid;
			memcpy(pk.data, payload, payload_len);
			pk.send_timer = timer_create();
			timer_tic(pk.send_timer);
			transmitted_packets.push_back(pk);
			// transmit frame
			txcvr.transmit_packet(header, payload, payload_len, ms, fec0, fec1);
		}

    } // packet loop
 
    // sleep for a small amount of time to allow USRP buffers
    // to flush
	
    usleep(200000);

	txcvr.stop_rx();
    //finished
    printf("usrp data transfer complete\n");


	std::cout << "Received " << received_acks << " acks." << std::endl;
	std::cout << "Received " << received_nacks << " nacks." << std::endl;
	std::cout << timeouts << " packets timed out and were retransmitted." << std::endl;
    printf("done.\n");
    return 0;
}

