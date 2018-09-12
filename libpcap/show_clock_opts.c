//
// Report the libpcap clock options for the given iface
//
#include <stdio.h>
#include <pcap/pcap.h>

void usage()
{
  printf("Usage: show_clock_opts <device>\n");
}

int main(int argc, char *argv[])
{
  pcap_t *hdl;
  int *types;
  int ntypes;
  int i;

  if (argc != 2) {
    usage();
    return 1;
  }

  hdl = pcap_create(argv[1], NULL);
  if (hdl == NULL) {
    fprintf(stderr, "Failed to get pcap handle\n");
    return 1;
  }

  if ((ntypes = pcap_list_tstamp_types(hdl, &types))
        == PCAP_ERROR) {
    fprintf(stderr, "Failed to get time stamp types\n");
    return 1;
  }

  if (ntypes == 0) {
    fprintf(stdout, "Clock not settable\n");
    return 0;
  }

  for (i = 0; i < ntypes; i++) {
    fprintf(stdout, "%s\n%s\n\n",
        pcap_tstamp_type_val_to_name(types[i]),
        pcap_tstamp_type_val_to_description(types[i]));
  }

  pcap_free_tstamp_types(types);
}
