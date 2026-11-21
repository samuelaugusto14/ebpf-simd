// SPDX-License-Identifier: Apache-2.0

#ifndef UBPF_DEMOS_TRACE_READER_H
#define UBPF_DEMOS_TRACE_READER_H

#include <pcap/pcap.h>

#define MAX_SOA_SIZE 1000 * 1000

typedef struct
{
    pcap_t* capture;
} trace_reader_t;

typedef struct
{
    const struct pcap_pkthdr* header;
    const unsigned char* data;
} trace_packet_t;

/* Open an offline capture. Return -1 and set errbuf on failure. */
int
trace_reader_open(trace_reader_t* reader, const char* path, char errbuf[PCAP_ERRBUF_SIZE]);

int
trace_reader_datalink(const trace_reader_t* reader);

/*
 * Return 1 for a packet, 0 at EOF or -1 on error (with errbuf set).
 * The packet is borrowed until the next read or close. Only caplen bytes
 * are available in data, even if the original length (len) is larger.
 */
int
trace_reader_next(trace_reader_t* reader, trace_packet_t* packet, char errbuf[PCAP_ERRBUF_SIZE]);

void
trace_reader_close(trace_reader_t* reader);

typedef void (*trace_packet_callback)(const struct pcap_pkthdr* header, const unsigned char* data, void* context);

/*
 * Invoke callback once per packet in capture order. The header and data
 * are valid only during the callback. Read at most header->caplen bytes
 * from data. The original packet length is header->len.
 *
 * Return 0 at end of file or -1 on an open or read error. On failure,
 * errbuf contains the error message. Packets read before an error have
 * already been passed to callback.
 */
int
trace_read(const char* path, trace_packet_callback callback, void* context, char errbuf[PCAP_ERRBUF_SIZE]);

#endif
