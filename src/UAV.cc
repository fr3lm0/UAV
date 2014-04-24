#include <iostream>
#include <complex>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <liquid/liquid.h>

#include <uhd/usrp/multi_usrp.hpp>
 
#include <liquid/ofdmtxrx.h>
#include "timer.h"

#define lock(s) pthread_mutex_lock(s)
#define unlock(s) pthread_mutex_unlock(s)

static bool verbose;

std::list<unsigned int> acks_to_send;
std::list<unsigned int> nacks_to_send;

pthread_mutex_t acks_to_send_mutex;
pthread_mutex_t nacks_to_send_mutex;

unsigned char fb_header[8];
unsigned char fb_payload[1];

timer rx_timer;
timer packet_arrival_timer;
bool first_packet_arrived = false;
float total_elapsed_time;
// data counters
unsigned int num_frames_detected;
unsigned int num_valid_headers_received;
unsigned int num_valid_packets_received;
unsigned int num_valid_bytes_received;

// callback function
int callback(unsigned char *  _header,
             int              _header_valid,
             unsigned char *  _payload,
             unsigned int     _payload_len,
             int              _payload_valid,
             framesyncstats_s _stats,
			 void *           _userdata)
{

	if (_header_valid) 
	{
		unsigned int packet_id = (_header[0] << 8 | _header[1]);
		//simulate missing 10% of packets entirely to trigger timeouts on tx side
		bool missed = rand() % 10 == 3 ? true : false;
		if(missed)
			std::cout << "missed packet " << packet_id << std::endl;
		else
		{
			timer_tic(packet_arrival_timer);
			if(!first_packet_arrived)
			{
				timer_tic(rx_timer);
				first_packet_arrived = true;
			}
			else
			{
				total_elapsed_time = timer_toc(rx_timer);
			}

			num_valid_headers_received++;
			//simulate 10% bad payloads to make sure we send some nacks
			bool still_valid = rand() % 10 != 3 ? true : false;
			if (_payload_valid && still_valid) 
			{
				lock(&acks_to_send_mutex);
				acks_to_send.push_back(packet_id);
				unlock(&acks_to_send_mutex);
				if(verbose)printf("rx packet id: %6u\n", packet_id);
				num_valid_packets_received++;
				num_valid_bytes_received += _payload_len;
			}
			else
			{
				if(verbose)printf("rx packet id: %6u", packet_id);
				lock(&nacks_to_send_mutex);
				nacks_to_send.push_back(packet_id);
				unlock(&nacks_to_send_mutex);
				printf(" PAYLOAD INVALID\n");
			}
		}
	} 
	else 
	{
		printf("HEADER INVALID\n");
	}
	// update global counters
	num_frames_detected++;

    return 0;
}

void usage() {
	printf("Transmission options:\n");
	printf("  --tx-freq             Set the transmission frequency\n");
	printf("                                [Default: 2.495 GHz]\n");
	printf("  --rx-freq             Set the reception frequency\n");
	printf("                                [Default: 2.497 GHz]\n");
	printf("  --tx-sw-gain              Set the software transmit gain\n");
	printf("                                [Default: -12 db]\n");
	printf("  --tx-hw-gain              Set the hardware transmit gain\n");
	printf("                                [Default: 40 db]\n");
	printf("  --rx-hw-gain              Set the hardware reception gain\n");
	printf("                                [Default: 20 db]\n");
	printf("  --mod-scheme              Set the modulation scheme to use for transmission\n");
	printf("                                [Default: QPSK]\n");
	printf("  Available options:\n");
	liquid_print_modulation_schemes();
	printf("\n");
	printf("  --inner-fec               Set the inner FEC scheme\n");
	printf("                                [Default: none]\n");
	printf("  --outer-fec               Set the outer FEC scheme\n");
	printf("                                [Default: V29P23\n");
	printf("  Available options:\n");
	liquid_print_fec_schemes();
	printf("\n");
	printf("OFDM options:\n");
	printf("  --num-subcarriers         Set the number of OFDM subcarriers\n");
	printf("                                [Default: 48]\n");
	printf("  --cyclic-prefix-len           Set the OFDM cyclic prefix length\n");
	printf("                                [Default: 6]\n");
	printf("  --taper-len               Set the OFDM taper length\n");
	printf("                                [Default: 4]\n");
	printf("Miscellaneous options:\n");
	printf("  --rx-timeout          Set the time to wait to quit after not receiving any packets\n");
	printf("                                [Default: 3.0 seconds]\n");
	printf("  --verbose             Enable extra output\n");
	printf("                                [Default: false]\n");
	printf("  --help                Display this help message\n");
	exit(0);
}

int main (int argc, char **argv)
{
	// command-line options
	verbose = false;

	float frequency = 2.495e9;
	float bandwidth = 500e3f;
	float frequency_separation = 4 * bandwidth;
	float uhd_rxgain = 20.0;
	double txgain_dB = -12.0f;          // software tx gain [dB]
	double uhd_txgain = 40.0;  

    // ofdm properties
    unsigned int M = 48;                // number of subcarriers
    unsigned int cp_len = 6;            // cyclic prefix length
    unsigned int taper_len = 4;         // taper length

    int debug_enabled =  0;             // enable debugging?

	modulation_scheme ms = LIQUID_MODEM_QPSK;// modulation scheme
	unsigned int payload_len = 256;        // original data message length
	fec_scheme fec0 = LIQUID_FEC_NONE;      // fec (inner)
	fec_scheme fec1 = LIQUID_FEC_CONV_V29P23; // fec (outer)
	rx_timer = timer_create();

	float rx_timeout = 3.0;
	packet_arrival_timer = timer_create();


    float rx_frequency = frequency;
    float tx_frequency = rx_frequency + frequency_separation;
    
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
    		{"mod-scheme",			required_argument, 0, 'i'},
			{"inner-fec",			required_argument, 0, 'j'},
			{"outer-fec",			required_argument, 0, 'k'},
			{"rx-timeout",	        required_argument, 0, 'l'},
            {"help",                no_argument,       0, 'm'},
            {"verbosity",           required_argument, 0, 'n'},
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
				ms = liquid_getopt_str2mod(optarg);
				if(ms == LIQUID_MODEM_UNKNOWN)
				{
					std::cout << "Modulation scheme " << ms << " not supported. Using QPSK." << std::endl;
					ms = LIQUID_MODEM_QPSK;
				}
				break;
			case 'j' :
				fec0 = liquid_getopt_str2fec(optarg);
				if(fec0 == LIQUID_FEC_UNKNOWN)
				{
					std::cout << "FEC scheme " << fec0 << " not supported. Using none." << std::endl;
					fec0 = LIQUID_FEC_NONE;
				}
				break;
			case 'k' :
				fec1 = liquid_getopt_str2fec(optarg);
				if(fec1 == LIQUID_FEC_UNKNOWN)
				{
					std::cout << "FEC scheme " << fec1 << " not supported. Using none." << std::endl;
					fec1 = LIQUID_FEC_NONE;
				}
				break;
			case 'l' :
				rx_timeout = atof(optarg);
				break;
			case 'm' :
				usage();
				break;
			case 'n' :
				verbose = true;
				break;
				
		}
	
	}

	std::cout << "tx freq: " << tx_frequency << std::endl;
	std::cout << "rx freq: " << rx_frequency << std::endl;
    
	if (cp_len == 0 || cp_len > M) {
        fprintf(stderr,"error: %s, cyclic prefix must be in (0,M]\n", argv[0]);
        exit(1);
    }

    // create transceiver object
    unsigned char * p = NULL;   // default subcarrier allocation
    ofdmtxrx txcvr(M, cp_len, taper_len, p, callback,(void*)&txcvr);

    // set properties
    txcvr.set_rx_freq(rx_frequency);
    txcvr.set_rx_rate(bandwidth);
    txcvr.set_rx_gain_uhd(uhd_rxgain);

	txcvr.set_tx_freq(tx_frequency);
	txcvr.set_tx_rate(bandwidth);
	txcvr.set_tx_gain_soft(txgain_dB);
	txcvr.set_tx_gain_uhd(uhd_txgain);

    // enable debugging on request
    if (debug_enabled)
        txcvr.debug_enable();

    // reset counters
    num_frames_detected=0;
    num_valid_headers_received=0;
    num_valid_packets_received=0;
    num_valid_bytes_received=0;

	unsigned char header[8];
	unsigned char payload[payload_len];
    
	// run conditions
    int continue_running = 1;
    timer t0 = timer_create();
    timer_tic(t0);

    // start receiver
    txcvr.start_rx();

    while (continue_running) {
		lock(&acks_to_send_mutex);
		while(acks_to_send.size() > 0)
		{
			header[0] = (acks_to_send.front() >> 8) & 0xff;
			header[1] = (acks_to_send.front()     ) & 0xff;
			header[2] = 0;
			txcvr.transmit_packet(header, payload, 0, LIQUID_MODEM_BPSK, LIQUID_FEC_CONV_V29P23, LIQUID_FEC_RS_M8);
			acks_to_send.pop_front();
		}
		unlock(&acks_to_send_mutex);
   
		lock(&nacks_to_send_mutex);
		while(nacks_to_send.size() > 0)
		{
			header[0] = (nacks_to_send.front() >> 8) & 0xff;
			header[1] = (nacks_to_send.front()     ) & 0xff;
			header[2] = 1;
			txcvr.transmit_packet(header, payload, 0, LIQUID_MODEM_BPSK, LIQUID_FEC_CONV_V29P23, LIQUID_FEC_RS_M8);
			nacks_to_send.pop_front();
		}
		unlock(&nacks_to_send_mutex);
		// sleep for 100 ms and check state
        usleep(100000);
		if(first_packet_arrived && timer_toc(packet_arrival_timer) > rx_timeout)
		{
			std::cout << "no packets received for " << rx_timeout << " seconds, quitting." << std::endl;
			continue_running = 0;
		}
    }

    // stop receiver
    printf("ofdmflexframe_rx stopping receiver...\n");
    txcvr.stop_rx();
 
    // compute actual run-time
    //float runtime = timer_toc(t0);
	//compute runtime = time of last packet arrival - time of first packet arrival
	float runtime = total_elapsed_time;
    // print results
    float data_rate = num_valid_bytes_received * 8.0f / runtime;
    float percent_headers_valid = (num_frames_detected == 0) ?
                          0.0f :
                          100.0f * (float)num_valid_headers_received / (float)num_frames_detected;
    float percent_packets_valid = (num_frames_detected == 0) ?
                          0.0f :
                          100.0f * (float)num_valid_packets_received / (float)num_frames_detected;
    printf("    frames detected     : %6u\n", num_frames_detected);
    printf("    valid headers       : %6u (%6.2f%%)\n", num_valid_headers_received,percent_headers_valid);
    printf("    valid packets       : %6u (%6.2f%%)\n", num_valid_packets_received,percent_packets_valid);
    printf("    bytes received      : %6u\n", num_valid_bytes_received);
    printf("    run time            : %f s\n", runtime);
    printf("    data rate           : %8.4f kbps\n", data_rate*1e-3f);

    // destroy objects
    timer_destroy(t0);

    return 0;
}

