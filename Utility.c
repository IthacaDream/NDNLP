#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <fcntl.h>
#include <net/if_dl.h>
#include <ifaddrs.h>
#include "ndnld.h"

void* ByteArray_clone(void* src, size_t len) {
	void* dst = malloc(len);
	return memcpy(dst, src, len);
}

char* String_clone(char* src) {
	size_t len = 1 + strlen(src);
	return (char*)ByteArray_clone(src, len);
}

DateTime DateTime_mockedNow = DateTime_noMock;

DateTime DateTime_now(void) {
	if (DateTime_mockedNow != DateTime_noMock) return DateTime_mockedNow;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void DateTime_mockNow(DateTime mocked) {
	DateTime_mockedNow = mocked;
}

SockAddr SockAddr_ctor(void) {
	SockAddr self = ALLOCSELF;
	self->addrlen = sizeof(struct sockaddr_storage);
	self->addr = (struct sockaddr*)calloc(1, self->addrlen);
	return self;
}

SockAddr SockAddr_create(void* addr, socklen_t addrlen) {
	SockAddr self = SockAddr_ctor();
	self->addrlen = addrlen;
	memcpy(self->addr, addr, addrlen);
	return self;
}

void SockAddr_dtor(SockAddr self) {
	free(self->addr);
	ccn_charbuf_destroy(&(self->hashkey));
	free(self);
}

void SockAddr_clear(SockAddr self) {
	self->addrlen = sizeof(struct sockaddr_storage);
	memset(self->addr, 0, self->addrlen);
}

struct sockaddr* SockAddr_addr(SockAddr self) {
	return self->addr;
}

socklen_t SockAddr_addrlen(SockAddr self) {
	return self->addrlen;
}

socklen_t* SockAddr_addrlenp(SockAddr self) {
	return &(self->addrlen);
}

struct ccn_charbuf* SockAddr_hashkey(SockAddr self) {
	if (self->hashkey == NULL) self->hashkey = ccn_charbuf_create();
	else ccn_charbuf_reset(self->hashkey);
	void* dbg = &(self->addr->sa_family);
	unsigned char* moved = (unsigned char*)dbg - 1;
	//printf("switch() on self->addr->sa_family-1 = %04x at addr: %p\n", self->addr->sa_family, &(self->addr->sa_family));
	//printf("switch() on self->addr->sa_family-1 = %04x at addr: %p\n", *moved, moved);
	//switch (self->addr->sa_family) {
	switch (*moved) {
		case AF_INET6: {
			struct sockaddr_in6* sin6 = (struct sockaddr_in6*)self->addr;
			ccn_charbuf_append(self->hashkey, &(sin6->sin6_addr), sizeof(struct in6_addr));
			ccn_charbuf_append(self->hashkey, &(sin6->sin6_port), sizeof(in_port_t));
			} break;
//#ifdef __linux__
		case AF_PACKET: {
			//printf("SockAddr_hashkey AF_PACKET\n"); /**/
			struct sockaddr_ll* sll = (struct sockaddr_ll*)self->addr;
		
		       	unsigned char* dbg = sll->sll_addr; /**/
			//printf("SockAddr_hashkey using:\n");
			//while ( dbg < sll->sll_addr + 6 ) {
			  //  printf("%02x:", *dbg);
			    //dbg++;
			//}	
			//printf("\n"); /**/
			printf("sll_halen before ccn_charbug_append: %d\n", sll->sll_halen);

			ccn_charbuf_append(self->hashkey, &(sll->sll_addr), sll->sll_halen);
			} break;
//#endif
		default:
			ccn_charbuf_append(self->hashkey, self->addr, self->addrlen);
			break;
	}
	if (self->hashkey == NULL ) /**/
	    printf("self->hashey == NULL!!\n"); /**/
	return self->hashkey;
}

bool SockAddr_equals(SockAddr self, SockAddr other) {
	if (self == NULL || other == NULL) return false;
	if (self->addrlen != other->addrlen) return false;
	return 0 == memcmp(self->addr, other->addr, self->addrlen);
}

SockAddr SockAddr_clone(SockAddr self) {
	SockAddr dst = SockAddr_ctor();
	SockAddr_copyto(self, dst);
	return dst;
}

void SockAddr_copyto(SockAddr self, SockAddr dst) {
	dst->addrlen = self->addrlen;
	memcpy(dst->addr, self->addr, dst->addrlen);
}

char* SockAddr_toString(SockAddr self) {
	static char buf[512];
	switch (self->addr->sa_family) {
		case AF_INET6: {
			struct sockaddr_in6* sin6 = (struct sockaddr_in6*)self->addr;
			char ntopbuf[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, &(sin6->sin6_addr), ntopbuf, INET6_ADDRSTRLEN);
			sprintf(buf, "SockAddr IPv6 %s port=%d", ntopbuf, be16toh(sin6->sin6_port));
			} break;
#ifdef __linux__
		case AF_PACKET: {
			struct sockaddr_ll* sll = (struct sockaddr_ll*)self->addr;
			sprintf(buf, "SockAddr Ethernet %s", ether_ntoa((struct ether_addr*)sll->sll_addr));
			} break;
#endif
		default:
			sprintf(buf, "SockAddr family=%x", self->addr->sa_family);
			break;
	}
	return buf;
}

StreamBuf StreamBuf_ctor(void) {
	return DgramBuf_ctor();
}

void StreamBuf_dtor(StreamBuf self) {
	DgramBuf_dtor(self);
}

void StreamBuf_prepend(StreamBuf self, void* data, size_t start, size_t len, BufMode mode) {
	DgramBuf_prepend(self, data, start, len, mode, NULL);
}

void StreamBuf_append(StreamBuf self, void* data, size_t start, size_t len, BufMode mode) {
	DgramBuf_append(self, data, start, len, mode, NULL);
}

bool StreamBuf_empty(StreamBuf self) {
	return self->head == NULL;
}

bool StreamBuf_get(StreamBuf self, void** pdata, size_t* plen) {
	return DgramBuf_get(self, pdata, plen, NULL);
}

void StreamBuf_consume(StreamBuf self, size_t len) {
	BufRec rec = self->head;
	while (len > 0 && rec != NULL) {
		rec->pos += len;
		if (rec->pos < rec->length) {
			len = 0;
		} else {
			len = rec->pos - rec->length;
			BufRec next = rec->next;
			BufRec_dtor(rec);
			rec = next;
		}
	}
	self->head = rec;
	if (rec == NULL) self->tail = NULL;
}

DgramBuf DgramBuf_ctor(void) {
	DgramBuf self = ALLOCSELF;
	return self;
}

void DgramBuf_dtor(DgramBuf self) {
	BufRec rec = self->head;
	while (rec != NULL) {
		BufRec next = rec->next;
		BufRec_dtor(rec);
		rec = next;
	}
	free(self);
}

void DgramBuf_prepend(DgramBuf self, void* data, size_t start, size_t len, BufMode mode, SockAddr addr) {
	BufRec rec = BufRec_ctor(data, start, len, mode, addr);
	rec->next = self->head;
	self->head = rec;
	if (self->tail == NULL) self->tail = rec;
}

void DgramBuf_append(DgramBuf self, void* data, size_t start, size_t len, BufMode mode, SockAddr addr) {
	BufRec rec = BufRec_ctor(data, start, len, mode, addr);
	if (self->tail == NULL) {
		self->head = self->tail = rec;
	} else {

		self->tail = rec;
	}
}

bool DgramBuf_empty(DgramBuf self) {
	return self->head == NULL;
}

bool DgramBuf_get(DgramBuf self, void** pdata, size_t* plen, SockAddr addr) {
	BufRec rec = self->head;
	if (rec == NULL) {
		*pdata = NULL;
		*plen = 0;
		return false;
	}
	*pdata = rec->buffer + rec->pos;
	*plen = rec->length - rec->pos;
	if (addr != NULL && rec->addr != NULL) {
		SockAddr_copyto(rec->addr, addr);
	}
	return true;
}

void DgramBuf_consumeOne(DgramBuf self) {
	BufRec rec = self->head;
	if (rec == NULL) return;
	self->head = rec->next;
	BufRec_dtor(rec);
	if (self->head == NULL) self->tail = NULL;
}

BufRec BufRec_ctor(void* data, size_t start, size_t len, BufMode mode, SockAddr addr) {
	BufRec self = ALLOCSELF;
	if (mode == BufMode_clone) {
		self->buffer = (uint8_t*)malloc(len);
		self->length = len;
		memcpy(self->buffer, (uint8_t*)data + start, len);
		self->own = true;
	} else {
		self->buffer = (uint8_t*)data;
		self->pos = start;
		self->length = start + len;
		self->own = mode == BufMode_own;
	}
	if (addr != NULL) self->addr = SockAddr_clone(addr);
	return self;
}

void BufRec_dtor(BufRec self) {
	if (self->own) free(self->buffer);
	if (self->addr != NULL) SockAddr_dtor(self->addr);
	free(self);
}

PollMgr PollMgr_ctor(TimeSpan timeout) {
	PollMgr self = ALLOCSELF;
	self->capacity = 8;
	self->records = (PollMgrRec)calloc(self->capacity, sizeof(struct PollMgr_rec));
	self->fds = (struct pollfd*)calloc(self->capacity, sizeof(struct pollfd));
	self->timeout = (int)timeout;
	return self;
}

void PollMgr_dtor(PollMgr self) {
	free(self->records);
	free(self->fds);
	free(self);
}

void PollMgr_resize(PollMgr self, int capacity) {
	if (self->count > capacity || capacity < 1) return;
	int oldcapacity = self->capacity;
	PollMgrRec oldrecords = self->records;
	self->capacity = capacity;
	self->records = (PollMgrRec)calloc(self->capacity, sizeof(struct PollMgr_rec));
	int j = -1;
	for (int i = 0; i < oldcapacity; ++i) {
		if (oldrecords[i].fd == 0) continue;
		self->records[++j] = oldrecords[i];
	}
	free(oldrecords);
	free(self->fds);
	self->fds = (struct pollfd*)calloc(self->capacity, sizeof(struct pollfd));
}

void PollMgr_attach(PollMgr self, int fd, PollMgrCb cb, void* data) {
	if (self->count == self->capacity) {
		PollMgr_resize(self, self->capacity * 2);
	}
	for (int i = 0; i < self->capacity; ++i) {
		PollMgrRec rec = self->records + i;
		if (rec->fd == 0) {
			++self->count;
			rec->fd = fd;
			rec->data = data;
			rec->callback = cb;
			break;
		}
	}
}

void PollMgr_detach(PollMgr self, int fd, PollMgrCb cb, void* data) {
	for (int i = 0; i < self->capacity; ++i) {
		PollMgrRec rec = self->records + i;
		if (rec->fd == fd && rec->callback == cb && rec->data == data) {
			--self->count;
			rec->fd = 0;
			break;
		}
	}
	if (self->count < self->capacity / 4 && self->capacity > 16) {
		PollMgr_resize(self, self->capacity / 4);
	}
}

void PollMgr_poll(PollMgr self) {
	if (self->count == 0) return;
	nfds_t nfds = 0;
	for (int i = 0; i < self->capacity; ++i) {
		PollMgrRec rec = self->records + i;
		if (rec->fd == 0) continue;
		struct pollfd* fd = self->fds + nfds;
		fd->fd = rec->fd;
		fd->events = fd->revents = 0;
		(*(rec->callback))(rec->data, PollMgrEvt_prepare, fd);
		if (self->fds[nfds].events != 0) {
			rec->pfd = fd;
			nfds += 1;
		} else {
			rec->pfd = NULL;
		}
	}
	
	//int c; /**/
	//printf("\n\n");
	//for ( c = 0; c < self->count; c++ ){ /**/
	//    printf("--fd: %d, events = 0x%04x\n", self->fds[c].fd, self->fds[c].events); /**/
	//} /**/
	

	poll(self->fds, nfds, self->timeout);
	
	//int c; /**/
	//for ( c = 0; c < self->count; c++ ){ /**/
	//    printf("--fd: %d, revents = 0x%04x\n", self->fds[c].fd, self->fds[c].revents); /**/
	//} /**/

	for (int i = 0; i < self->capacity; ++i) {
		PollMgrRec rec = self->records + i;
		if (rec->fd == 0 || rec->pfd == NULL) continue;
		struct pollfd* fd = rec->pfd;
		bool hasError = (fd->revents & POLLERR) || (fd->revents & POLLNVAL);
		(*(rec->callback))(rec->data, hasError ? PollMgrEvt_error : PollMgrEvt_result, fd);
	}
}

NBS NBS_ctor(int sockR, int sockW, enum SocketType sock_type) {
	NBS self = ALLOCSELF;
	self->sock_type = sock_type;
	self->sockR = sockR;
	self->sockW = sockW;
	if (NBS_isDgram(self)) {
		self->dbufR = DgramBuf_ctor();
		self->dbufW = DgramBuf_ctor();
	} else {
		self->sbufR = StreamBuf_ctor();
		self->sbufW = StreamBuf_ctor();
	}
	return self;
}

void NBS_dtor(NBS self) {
	NBS_pollDetach(self);
	if (NBS_isDgram(self)) {
		DgramBuf_dtor(self->dbufR);
		DgramBuf_dtor(self->dbufW);
	} else {
		StreamBuf_dtor(self->sbufR);
		StreamBuf_dtor(self->sbufW);
	}
	if (self->closeSock) {
		close(self->sockR);
		if (self->sockW != self->sockR) close(self->sockW);
	}
	free(self);
}

bool NBS_isDgram(NBS self) {
	return self->sock_type == SockType_Dgram || self->sock_type == SockType_BPF;
}

int NBS_sockR(NBS self) {
	return self->sockR;
}

int NBS_sockW(NBS self) {
	return self->sockW;
}

bool NBS_error(NBS self) {
	return self->error;
}

void NBS_setSockClose(NBS self, bool close) {
	self->closeSock = close;
}

void NBS_pollAttach(NBS self, PollMgr pm) {
	PollMgr_attach(pm, self->sockR, &NBS_pollCb, self);
	if (self->sockR != self->sockW) {
		PollMgr_attach(pm, self->sockW, &NBS_pollCb, self);
	}
	self->pm = pm;
}

void NBS_pollDetach(NBS self) {
	if (self->pm == NULL) return;
	PollMgr_detach(self->pm, self->sockR, &NBS_pollCb, self);
	if (self->sockR != self->sockW) {
		PollMgr_detach(self->pm, self->sockW, &NBS_pollCb, self);
	}
	self->pm = NULL;
}

void NBS_setDataArrivalCb(NBS self, NBSCb cb, void* data) {
	self->dataArrivalCb = cb;
	self->dataArrivalCbData = data;
}

size_t NBS_read(NBS self, void* buf, size_t count, SockAddr srcaddr) {
	void* data; size_t len; size_t pos = 0;
	if (NBS_isDgram(self)) {
		if (DgramBuf_get(self->dbufR, &data, &len, srcaddr)) {
			if (count < len) len = count;
			memcpy(buf, data, len);
			DgramBuf_consumeOne(self->dbufR);
			return len;
		}
	} else {
		while (StreamBuf_get(self->sbufR, &data, &len) && pos < count) {
			if (count - pos < len) len = count - pos;
			memcpy((uint8_t*)buf + pos, data, len);
			pos += len;
			StreamBuf_consume(self->sbufR, len);
		}
	}
	if (pos < count && self->canR) {
#ifdef ENABLE_ETHER_BPF
	        if ( self->sock_type == SockType_BPF ) {
	            int res = 0;
	            struct bpf_hdr* bpf_packet;
	            int index = 0;
	            //size_t* first_len;
	            struct ether_header* eh;
		    //printf(" [self->bpf_len] = %d\n", self->bpf_len); 
		    printf("about to read from bpf socket...\n");
	            if ( (res = read(self->sockR, buf, self->bpf_len)) > 0 ) {
	            	 char* ptr = (char*)buf;
			 printf("\n\n[res] %d bytes read into %p from bpf device: %d\n", res, buf, self->sockR);
	            	 while ( ptr < ((char*)buf + res) ) {
	            	 	printf("reading new packet in buffer located at %p\n", buf);
				bpf_packet = (struct bpf_hdr*)ptr;
	            	 	data = (char*)bpf_packet + bpf_packet->bh_hdrlen + sizeof(struct ether_header);
	            	 	int datalen = bpf_packet->bh_datalen - sizeof(struct ether_header);
				
				printf("--- bpf_hdr: caplen: %d, datalen: %d, hdrlen:%d\n", bpf_packet->bh_caplen, bpf_packet->bh_datalen, bpf_packet->bh_hdrlen);
				printf("--- sizeof ether header: %d\n", (int)sizeof(struct ether_header));
				//printf("bpf_packet points to: %p\n", bpf_packet);
				eh = (struct ether_header*)((char*)bpf_packet + bpf_packet->bh_hdrlen);

				struct ether_addr src_MAC;
				struct ether_addr dest_MAC;
				
				printf("*** ETHERNET PACKET RECIEVED ***\n");
				memcpy(&src_MAC, &eh->ether_shost, sizeof(src_MAC));
				memcpy(&dest_MAC, &eh->ether_dhost, sizeof(dest_MAC));
				printf("   src MAC:  %s\n", ether_ntoa(&src_MAC));
				printf("   dest MAC:  %s\n", ether_ntoa(&dest_MAC));
				printf("   eth type: %4X\n", htons(eh->ether_type));
				printf("***          ******          ***\n");
				unsigned char* dbg = (unsigned char*)eh;
				printf(" *** full ethernet frame ***\n");
				while ( dbg < (unsigned char*)eh + datalen + sizeof(struct ether_header) ) {
				    printf("%02x", *dbg);
				    dbg++;
				}
				printf("\n***		***\n");
				dbg = data;
				printf("%d byte payload, will be returned in %p:\n", datalen, buf);
				while ( dbg < (unsigned char*)data + datalen ) {
				    printf("%02x", *dbg);
				    dbg++;
				}
				printf("\n");
				//exit(1);
				uint8_t* sadll = ((struct sockaddr_ll*)(SockAddr_addr(srcaddr)))->sll_addr;
	            	 	memcpy(sadll, eh->ether_shost, sizeof(((struct sockaddr_ll*)(SockAddr_addr(srcaddr)))->sll_addr));
				((struct sockaddr_ll*)(SockAddr_addr(srcaddr)))->sll_family = AF_PACKET;	
				((struct sockaddr_ll*)(SockAddr_addr(srcaddr)))->sll_halen = 6;

				printf("sll_family = %04x\n", ((struct sockaddr_ll*)(SockAddr_addr(srcaddr)))->sll_family);
				
				unsigned char* addrdbg = (unsigned char*)sadll;
			        printf("SRC MAC:\n");
				while (addrdbg < sadll + 6) {
				    printf("%02x", *addrdbg);
				    addrdbg++; 
				}
	            	        printf("\n");	
				DgramBuf_append(self->dbufR, data, 0, datalen, BufMode_clone, srcaddr); 
	            	 	ptr += BPF_WORDALIGN(bpf_packet->bh_hdrlen + bpf_packet->bh_caplen);
	            	 }
	            	 /* get first packet */
			 if (DgramBuf_get(self->dbufR, &data, &len, srcaddr)) {
			 	memcpy(buf, data, len);
			 	DgramBuf_consumeOne(self->dbufR);
			 	//return len;
			 }


			 //printf("DgramBuf_get called, then put %d byte packet into %p\n", (int)len, buf);
			 //unsigned char* dbg = buf;
			 //while ( dbg < (unsigned char*)buf + len ) {
			 //    printf("%02x", *dbg);
			 //    dbg++;
		   	 //}
			 //printf("\n");
			 //printf("NBS_read returning packet at %p\n", buf);
	            	 //pos = *first_len;
	             }
		     if (res == -1) {
		     	 if (errno == EAGAIN || errno == EWOULDBLOCK) self->canR = false;
		     	 else self->error = true;
		     } else {
		     	 pos += len;
		     }
		     printf("NBS_read returning %d bytes\n", (int)pos);
		     return pos;
	        }
#endif // ENABLE_ETHER_BPF
//#ifdef ENABLE_ETHER
       	        void* recvbuf = (uint8_t*)buf + pos;
		size_t recvbuflen = count - pos;
		ssize_t res;
		if (NBS_isDgram(self) && srcaddr != NULL) {
			res = recvfrom(self->sockR, recvbuf, recvbuflen, 0, SockAddr_addr(srcaddr), SockAddr_addrlenp(srcaddr));
		} else {
			res = read(self->sockR, recvbuf, recvbuflen);
		}
		if (res == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) self->canR = false;
			else self->error = true;
		} else {
			pos += res;
		}
	return pos;
//#endif // ENABLE_ETHER
	}
	return 0;
}
void NBS_pushback(NBS self, void* data, size_t start, size_t len, SockAddr srcaddr) {
	if (NBS_isDgram(self)) {
		DgramBuf_prepend(self->dbufR, data, start, len, BufMode_own, srcaddr);
	} else {
		StreamBuf_prepend(self->sbufR, data, start, len, BufMode_own);
	}
}

void NBS_write(NBS self, void* data, size_t start, size_t len, SockAddr dstaddr) {
	printf("NBS_write called, device: %d\n", self->sockW); /**/
	if (NBS_isDgram(self)) {
		DgramBuf_append(self->dbufW, data, start, len, BufMode_own, dstaddr);
		if (self->sock_type == SockType_BPF) {
		    self->canW = true;
		    NBS_deferredWrite(self);
		}
	} else {
		StreamBuf_append(self->sbufW, data, start, len, BufMode_own);
	}
}

void NBS_pollCb(void* pself, PollMgrEvt evt, struct pollfd* fd) {
	NBS self = (NBS)pself;
	switch (evt) {
		case PollMgrEvt_prepare:
			//printf("PollMgrEvt prepare\n");
			if (self->sockR == fd->fd) {
				fd->events |= POLLIN;
			}

			//printf(" --- fd = %d\n", fd->fd); /**/
			//if ( self->sockW == fd->fd ) /**/
			//    printf(" --- self->sockW == fd->fd\n"); /**/
			//if ( NBS_isDgram(self) )
			//    printf(" --- NBS_isDgram\n"); /**/
			//if ( NBS_isDgram(self) ) { /**/
			//    printf(" --- is Dgram\n"); /**/ 
			//    if ( !DgramBuf_empty(self->dbufW) ) /**/
			//        printf(" --- !DgramBuf_empty\b"); /**/
			//}
			//printf("End PollMgrEvt\n"); /**/ 

			if (self->sockW == fd->fd && ((NBS_isDgram(self)) ? !DgramBuf_empty(self->dbufW) : !StreamBuf_empty(self->sbufW))) {
				//printf("events for fd %d set to POLLOUT\n", self->sockW);
				fd->events |= POLLOUT;
			}
			break;
		case PollMgrEvt_result:
			if (fd->fd == self->sockR) {
				self->canR = fd->revents & POLLIN;
				if (self->canR) {
					if (self->dataArrivalCb != NULL) {
						(*(self->dataArrivalCb))(self->dataArrivalCbData, self);
					}
				}
			}
			if (fd->fd == self->sockW) {
				self->canW = fd->revents & POLLOUT;
				if (self->canW) NBS_deferredWrite(self);
			}
			break;
		case PollMgrEvt_error:
			self->error = true;
			break;
	}
}

void NBS_deferredWrite(NBS self) {
	printf("\n\nNBS_deferredWrite called\n"); /**/
	void* data; size_t len; ssize_t res;
	SockAddr dstaddr; size_t eth_header_len; void* frame;
	uint8_t* sadll; struct ether_header* eh;

	switch ( self->sock_type ) {
	    
//#ifdef ENABLE_ETHER
	    
	    case SockType_Dgram:
		dstaddr = SockAddr_ctor();
		while (self->canW && DgramBuf_get(self->dbufW, &data, &len, dstaddr)) {
			res = sendto(self->sockW, data, len, 0, SockAddr_addr(dstaddr), SockAddr_addrlen(dstaddr));
			if (res == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) self->canW = false;
				else self->error = true;
				break;
			} else {
				DgramBuf_consumeOne(self->dbufW);
			}
		}
		SockAddr_dtor(dstaddr);
		break;
//#endif

//#ifdef ENABLE_ETHER_BPF
	    
	    case SockType_BPF:
		printf("deferredWrite: SockType_BPF\n"); /**/
		dstaddr = SockAddr_ctor();
		while (self->canW && DgramBuf_get(self->dbufW, &data, &len, dstaddr)) {
			eth_header_len = sizeof(struct ether_header);
			frame = malloc(len+eth_header_len);
			sadll = ((struct sockaddr_ll*)(SockAddr_addr(dstaddr)))->sll_addr;
			eh = (struct ether_header*)frame;
			memcpy(eh->ether_dhost, sadll, sizeof(eh->ether_dhost));
			eh->ether_type = ntohs(LinkC_eth_proto);
			memcpy(frame+eth_header_len, data, len);
			
			printf("just before writing to BPF device: %d\n", self->sockW); /**/
			struct ether_addr src_MAC;
			struct ether_addr dest_MAC;	
			printf("*** SENDING ETHERNET PACKET ***\n");
			memcpy(&src_MAC, &eh->ether_shost, sizeof(src_MAC));
			memcpy(&dest_MAC, &eh->ether_dhost, sizeof(dest_MAC));
			printf("   src MAC:  %s\n", ether_ntoa(&src_MAC));
			printf("   dest MAC:  %s\n", ether_ntoa(&dest_MAC));
			printf("   eth type: %4X\n", htons(eh->ether_type));
			unsigned char* dbg = (unsigned char*)frame + eth_header_len;
			while ( dbg < (unsigned char*)frame + len + eth_header_len ) {
			    printf("%02x", *dbg);
			    dbg++;
			}
			printf("\n");
			printf("***          ******          ***\n");
			
			res = write(self->sockW, frame, len+eth_header_len);
			free(frame);	
			if (res == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) self->canW = false;
				else self->error = true;
				break;
			} else {
				DgramBuf_consumeOne(self->dbufW);
			}
		}
		SockAddr_dtor(dstaddr);
		break;
//#endif

	    default:
		while (self->canW && StreamBuf_get(self->sbufW, &data, &len)) {
			res = write(self->sockW, data, len);
			if (res == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) self->canW = false;
				else self->error = true;
				break;
			} else {
				StreamBuf_consume(self->sbufW, (size_t)res);
			}
		}
	}
	
	
	
	/*if (self->sock_type == SockType_Dgram) {
		SockAddr dstaddr = SockAddr_ctor();
		while (self->canW && DgramBuf_get(self->dbufW, &data, &len, dstaddr)) {
#ifdef ENABLE_ETHER
			res = sendto(self->sockW, data, len, 0, SockAddr_addr(dstaddr), SockAddr_addrlen(dstaddr));
#elif defined(ENABLE_ETHER_BPF)
			size_t eth_header_len = sizeof(struct ether_header);
			void* frame = malloc(len+eth_header_len);
			uint8_t* sadll = ((struct sockaddr_ll*)(SockAddr_addr(dstaddr)))->sll_addr;
			//struct sockaddr_ll* sadll = (struct sockaddr_ll*)malloc(sizeof(struct sockaddr_ll));
			//uint8_t* dstaddr_ll = sadll->sll_addr;
			//uint8_t* dstaddr_ll = NULL;
			struct ether_header* eh = (struct ether_header*)frame;
			memcpy(eh->ether_dhost, sadll, sizeof(eh->ether_dhost));
			memcpy(frame+eth_header_len, data, len);
			res = write(self->sockW, frame, len+eth_header_len);
						
#endif
			if (res == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) self->canW = false;
				else self->error = true;
				break;
			} else {
				DgramBuf_consumeOne(self->dbufW);
			}
		}
		SockAddr_dtor(dstaddr);
	} else {
		while (self->canW && StreamBuf_get(self->sbufW, &data, &len)) {
			res = write(self->sockW, data, len);
			if (res == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) self->canW = false;
				else self->error = true;
				break;
			} else {
				StreamBuf_consume(self->sbufW, (size_t)res);
			}
		}
	}
	*/
}

uid_t CapsH_ruid;
uid_t CapsH_euid;

void CapsH_drop() {
	CapsH_ruid = getuid();
	CapsH_euid = geteuid();
	seteuid(CapsH_ruid);
}

#ifdef ENABLE_ETHER
int CapsH_createPacketSock(int socket_type, int protocol) {
	int sock;
	seteuid(CapsH_euid);
	sock = socket(AF_PACKET, socket_type, protocol);
	seteuid(CapsH_ruid);
	return sock;
}
#elif defined(ENABLE_ETHER_BPF)
int CapsH_createBPF(char* ifname) {
	int bpf = 0;
	int i;
	char filename[11] = {0};
	
	seteuid(CapsH_euid);

	for ( i = 0; i < 99; i++ ) { // make sure we test every bpf file
	    sprintf(filename, "/dev/bpf%i", i);
	    bpf = open(filename, O_RDWR);
	    if ( bpf >= 0 ) {
		printf("opened file: %s\n", filename);
		struct ifreq bound_if;
	        strcpy(bound_if.ifr_name, ifname);
		// attach to given interface 
		if ( ioctl(bpf, BIOCSETIF, &bound_if) > 0 ) {
             	    printf("Error binding bpf device to %s\n", ifname);
                    perror("interface error");
		    return -1;
		}
		printf("bound bpf to %s\n", ifname);
				
		int opt = 1;
		// set device to nonblocking
		if( ioctl(bpf, FIONBIO, &opt) == -1 ) {
		    perror("error setting nonblockig BPF\n");
		    return -1;
		}
		// return immediately when a packet is received
		if( ioctl( bpf, BIOCIMMEDIATE, &opt ) == -1 ) {
		    perror("error setting BIOCIMMEDIATE mode\n");
                    return -1;
		}
		
	        /* 
		 * set up filter rules
		   we will filter by destination MAC address and ether type
		*/
				  //struct ether_addr src_MAC;
				  //struct ether_addr dest_MAC;
				  //
				  //printf("*** ETHERNET PACKET RECIEVED ***\n");
				  //memcpy(&src_MAC, &eh->ether_shost, sizeof(src_MAC));
				  //memcpy(&dest_MAC, &eh->ether_dhost, sizeof(dest_MAC));
				  //printf("   src MAC:  %s\n", ether_ntoa(&src_MAC));
				  //printf("   dest MAC:  %s\n", ether_ntoa(&dest_MAC));
				  //printf("   eth type: %4X\n", htons(eh->ether_type));
				  //printf("***          ******          ***\n");
		
		//get mac address of ifname
		struct ifaddrs *ifap, *ifaptr;
		unsigned char thishwaddr[6];
		unsigned char* ptr;

	 	if ( getifaddrs(&ifap) == 0 ) {
		    for ( ifaptr = ifap; ifaptr != NULL; ifaptr = ifaptr->ifa_next ) {
		        if( ((ifaptr)->ifa_addr)->sa_family == AF_LINK ) {
			    //printf("\n ethernet address of %s: %02x:%02x:%02x:%02x:%02x:%02x\n", ifaptr->ifa_name, *ptr, *(ptr+1), *(ptr+2),
					    										//*(ptr+3), *(ptr+4), *(ptr+5));
			    if ( strcmp(ifname, ifaptr->ifa_name) == 0 ) {
				printf("found if %s\n", ifaptr->ifa_name);
			        ptr = (unsigned char *)LLADDR((struct sockaddr_dl *)(ifaptr)->ifa_addr); 
				thishwaddr[0] = *ptr;
				thishwaddr[1] = *(ptr+1);
				thishwaddr[2] = *(ptr+2);
				thishwaddr[3] = *(ptr+3);
				thishwaddr[4] = *(ptr+4);
				thishwaddr[5] = *(ptr+5);
			    }
			}
		    }
		    freeifaddrs(ifap);
		}
		else {
		    printf("enable to get MAC address of %s\n", ifname);
		    return -1;
		}
	
		printf("hw addr of %s: %02x:%02x:%02x:%02x:%02x:%02x\n",ifname,thishwaddr[0],thishwaddr[1],thishwaddr[2],thishwaddr[3],thishwaddr[4],thishwaddr[5]);

    		struct bpf_program fcode = {0};
    		struct bpf_insn insns[] = {
		    // filter by destination address
		    BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 0),
		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, thishwaddr[0], 0, 13),
		    BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 1),
		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, thishwaddr[1], 0, 11),
		    BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 2),
		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, thishwaddr[2], 0, 9),
		    BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 3),
		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, thishwaddr[3], 0, 7),
		    BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 4),
		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, thishwaddr[4], 0, 5),
		    BPF_STMT(BPF_LD+BPF_B+BPF_ABS, 5),
		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, thishwaddr[5], 0, 3),
    		    // copy protocol value (byte offset 12 of ether frame) into accumulator register
    		    BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 12),
    		    // If accumulator register is equal to LinkC_eth_proto, execute following return STMT,
    		    // If not, jump to STMT that returns 0 (drop packet)
    		    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, LinkC_eth_proto, 0, 1),
    		    BPF_STMT(BPF_RET+BPF_K, (u_int)-1),
    		    BPF_STMT(BPF_RET+BPF_K, 0),
    		};	
    		/* Set the filter */
    		fcode.bf_len = sizeof(insns) / sizeof(struct bpf_insn);
    		fcode.bf_insns = &insns[0];
    		if( ioctl( bpf, BIOCSETF, &fcode ) < 0) {
		    printf("error setting BPF filter\n");
		    return ( -1 );
		}
	    	seteuid(CapsH_ruid);
		printf("bpf = %d\n", bpf);
		return bpf;
	    }
	}
	seteuid(CapsH_ruid);
	fprintf(stderr, "Error: no bpf files could be opened\n");
	return -1;
}
#endif
