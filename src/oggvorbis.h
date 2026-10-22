// oggvorbis.h: the parts of Xiph.Org's libogg 1.0 and libvorbis 1.0 public headers (<ogg/ogg.h>, <vorbis/codec.h>,
// BSD-licensed) the game uses. The PC links them from ogg.dll and vorbis.dll through import thunks
// (0055d5fe..0055d68e); the structure sizes are fixed by COggFileRead's layout (0x2014..0x22d8).

#ifndef OGGVORBIS_H
#define OGGVORBIS_H

typedef __int64 ogg_int64_t;
typedef short ogg_int16_t;

typedef struct {
	long endbyte;
	int  endbit;
	unsigned char *buffer;
	unsigned char *ptr;
	long storage;
} oggpack_buffer;

typedef struct {
	unsigned char *header;
	long header_len;
	unsigned char *body;
	long body_len;
} ogg_page;											// 0x10

typedef struct {
	unsigned char   *body_data;
	long    body_storage;
	long    body_fill;
	long    body_returned;
	int     *lacing_vals;
	ogg_int64_t *granule_vals;
	long    lacing_storage;
	long    lacing_fill;
	long    lacing_packet;
	long    lacing_returned;
	unsigned char    header[282];
	int              header_fill;
	int     e_o_s;
	int     b_o_s;
	long    serialno;
	long    pageno;
	ogg_int64_t  packetno;
	ogg_int64_t   granulepos;
} ogg_stream_state;									// 0x168

typedef struct {
	unsigned char *packet;
	long  bytes;
	long  b_o_s;
	long  e_o_s;
	ogg_int64_t  granulepos;
	ogg_int64_t  packetno;
} ogg_packet;										// 0x20

typedef struct {
	unsigned char *data;
	int storage;
	int fill;
	int returned;
	int unsynced;
	int headerbytes;
	int bodybytes;
} ogg_sync_state;									// 0x1c

typedef struct vorbis_info {
	int version;
	int channels;									// +0x04
	long rate;										// +0x08
	long bitrate_upper;
	long bitrate_nominal;
	long bitrate_lower;
	long bitrate_window;
	void *codec_setup;
} vorbis_info;										// 0x20

typedef struct vorbis_dsp_state {
	int analysisp;
	vorbis_info *vi;
	float **pcm;
	float **pcmret;
	int      pcm_storage;
	int      pcm_current;
	int      pcm_returned;
	int  preextrapolate;
	int  eofflag;
	long lW;
	long W;
	long nW;
	long centerW;
	ogg_int64_t granulepos;
	ogg_int64_t sequence;
	ogg_int64_t glue_bits;
	ogg_int64_t time_bits;
	ogg_int64_t floor_bits;
	ogg_int64_t res_bits;
	void       *backend_state;
} vorbis_dsp_state;									// 0x70

typedef struct vorbis_block {
	float  **pcm;
	oggpack_buffer opb;
	long  lW;
	long  W;
	long  nW;
	int   pcmend;
	int   mode;
	int         eofflag;
	ogg_int64_t granulepos;
	ogg_int64_t sequence;
	vorbis_dsp_state *vd;
	void               *localstore;
	long                localtop;
	long                localalloc;
	long                totaluse;
	struct alloc_chain *reap;
	long glue_bits;
	long time_bits;
	long floor_bits;
	long res_bits;
	void *internal;
} vorbis_block;										// 0x70

typedef struct vorbis_comment {
	char **user_comments;							// +0x00
	int   *comment_lengths;
	int    comments;
	char  *vendor;									// +0x0c
} vorbis_comment;									// 0x10

extern "C"
{
extern int      ogg_sync_init(ogg_sync_state *oy);
extern int      ogg_sync_clear(ogg_sync_state *oy);
extern char    *ogg_sync_buffer(ogg_sync_state *oy, long size);
extern int      ogg_sync_wrote(ogg_sync_state *oy, long bytes);
extern int      ogg_sync_pageout(ogg_sync_state *oy, ogg_page *og);
extern int      ogg_stream_pagein(ogg_stream_state *os, ogg_page *og);
extern int      ogg_stream_packetout(ogg_stream_state *os,ogg_packet *op);
extern int      ogg_stream_init(ogg_stream_state *os,int serialno);
extern int      ogg_stream_clear(ogg_stream_state *os);
extern int      ogg_page_eos(ogg_page *og);
extern int      ogg_page_serialno(ogg_page *og);

extern void     vorbis_info_init(vorbis_info *vi);
extern void     vorbis_info_clear(vorbis_info *vi);
extern void     vorbis_comment_init(vorbis_comment *vc);
extern void     vorbis_comment_clear(vorbis_comment *vc);
extern int      vorbis_block_init(vorbis_dsp_state *v, vorbis_block *vb);
extern int      vorbis_block_clear(vorbis_block *vb);
extern void     vorbis_dsp_clear(vorbis_dsp_state *v);
extern int      vorbis_synthesis_headerin(vorbis_info *vi,vorbis_comment *vc, ogg_packet *op);
extern int      vorbis_synthesis_init(vorbis_dsp_state *v,vorbis_info *vi);
extern int      vorbis_synthesis(vorbis_block *vb,ogg_packet *op);
extern int      vorbis_synthesis_blockin(vorbis_dsp_state *v,vorbis_block *vb);
extern int      vorbis_synthesis_pcmout(vorbis_dsp_state *v,float ***pcm);
extern int      vorbis_synthesis_read(vorbis_dsp_state *v,int samples);
}

#endif
