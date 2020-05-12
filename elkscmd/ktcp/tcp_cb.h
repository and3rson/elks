#ifndef TCP_CB_H
#define TCP_CB_H

void tcpcb_init(void);
struct tcpcb_list_s *tcpcb_clone(struct tcpcb_s *cb);
void tcpcb_buf_read(struct tcpcb_s *cb, __u8 *data, __u16 len);
void tcpcb_buf_write(struct tcpcb_s *cb, __u8 *data, __u16 len);
void tcpcb_expire_timeouts(void);
void tcpcb_push_data(void);
struct tcpcb_list_s *tcpcb_check_port(__u16 lport);
struct tcpcb_list_s *tcpcb_find_unaccepted(__u16 sock);
void tcpcb_rmv_all_unaccepted(struct tcpcb_s *cb);
struct tcpcb_s *tcpcb_getbynum(int num);

#endif
