// SPDX-License-Identifier: Apache-2.0

#include "trace_reader.h"

#include <stdio.h>

int
trace_reader_open(trace_reader_t* reader, const char* path, char errbuf[PCAP_ERRBUF_SIZE])
{
    reader->capture = pcap_open_offline(path, errbuf);
    return reader->capture ? 0 : -1;
}

int
trace_reader_datalink(const trace_reader_t* reader)
{
    return pcap_datalink(reader->capture);
}

int
trace_reader_next(trace_reader_t* reader, trace_packet_t* packet, char errbuf[PCAP_ERRBUF_SIZE])
{
    struct pcap_pkthdr* header;
    const unsigned char* data;
    int status;
    do {
        status = pcap_next_ex(reader->capture, &header, &data);
    } while (status == 0);

    if (status == 1) {
        *packet = (trace_packet_t){header, data};
        return 1;
    }

    if (status == PCAP_ERROR_BREAK) {
        errbuf[0] = '\0';
        return 0;
    }

    snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s", pcap_geterr(reader->capture));
    return -1;
}

void
trace_reader_close(trace_reader_t* reader)
{
    if (reader->capture) {
        pcap_close(reader->capture);
        reader->capture = NULL;
    }
}

int
trace_read(const char* path, trace_packet_callback callback, void* context, char errbuf[PCAP_ERRBUF_SIZE])
{
    trace_reader_t reader;
    if (trace_reader_open(&reader, path, errbuf) < 0) {
        return -1;
    }

    trace_packet_t packet;
    int status;
    while ((status = trace_reader_next(&reader, &packet, errbuf)) == 1) {
        callback(packet.header, packet.data, context);
    }

    trace_reader_close(&reader);
    return status;
}
