#include <debug.h>
#include <lwip/tcpip.h>

#include "lwip_glue.h"

static const char * const tcp_state_str[] = {
  "CLOSED",
  "LISTEN",
  "SYN_SENT",
  "SYN_RCVD",
  "ESTABLISHED",
  "FIN_WAIT_1",
  "FIN_WAIT_2",
  "CLOSE_WAIT",
  "CLOSING",
  "LAST_ACK",
  "TIME_WAIT"
};

/* lwIP raw API calls are serialized either by its core lock or by a callback on
 * the tcpip thread. Callers that hold the connection resource use callbacks to
 * preserve the core-lock/resource order; other synchronous calls use the core lock. */

extern KEVENT TerminationEvent;
extern NPAGED_LOOKASIDE_LIST QueueEntryLookasideList;

/* Required for ERR_T to NTSTATUS translation in receive error handling */
NTSTATUS TCPTranslateError(const err_t err);

void
LibTCPDumpPcb(PVOID SocketContext)
{
    struct tcp_pcb *pcb = (struct tcp_pcb*)SocketContext;
    unsigned int addr = lwip_ntohl(pcb->remote_ip.addr);

    DbgPrint("\tState: %s\n", tcp_state_str[pcb->state]);
    DbgPrint("\tRemote: (%d.%d.%d.%d, %d)\n",
    (addr >> 24) & 0xFF,
    (addr >> 16) & 0xFF,
    (addr >> 8) & 0xFF,
    addr & 0xFF,
    pcb->remote_port);
}

static
void
LibTCPEmptyQueue(PCONNECTION_ENDPOINT Connection)
{
    PLIST_ENTRY Entry;
    PQUEUE_ENTRY qp = NULL;

    ReferenceObject(Connection);

    while (!IsListEmpty(&Connection->PacketQueue))
    {
        Entry = RemoveHeadList(&Connection->PacketQueue);
        qp = CONTAINING_RECORD(Entry, QUEUE_ENTRY, ListEntry);

        /* The caller is in serialized lwIP core context, so this is safe. */
        pbuf_free(qp->p);

        ExFreeToNPagedLookasideList(&QueueEntryLookasideList, qp);
    }

    DereferenceObject(Connection);
}

void LibTCPEnqueuePacket(PCONNECTION_ENDPOINT Connection, struct pbuf *p)
{
    PQUEUE_ENTRY qp;

    qp = (PQUEUE_ENTRY)ExAllocateFromNPagedLookasideList(&QueueEntryLookasideList);
    qp->p = p;
    qp->Offset = 0;

    LockObject(Connection);
    InsertTailList(&Connection->PacketQueue, &qp->ListEntry);
    UnlockObject(Connection);
}

PQUEUE_ENTRY LibTCPDequeuePacket(PCONNECTION_ENDPOINT Connection)
{
    PLIST_ENTRY Entry;
    PQUEUE_ENTRY qp = NULL;

    if (IsListEmpty(&Connection->PacketQueue)) return NULL;

    Entry = RemoveHeadList(&Connection->PacketQueue);

    qp = CONTAINING_RECORD(Entry, QUEUE_ENTRY, ListEntry);

    return qp;
}

/* tcp_recved() takes a u16 count, so larger credits are fed in chunks */
static
void
LibTCPRecvedChunked(PTCP_PCB pcb, u32_t Length)
{
    while (pcb && Length > 0)
    {
        u16_t Chunk = (Length > 0xFFFF) ? 0xFFFF : (u16_t)Length;
        tcp_recved(pcb, Chunk);
        Length -= Chunk;
    }
}

static
void
LibTCPRecved(PCONNECTION_ENDPOINT Connection, u32_t Length, const int safe)
{
    if (safe)
    {
        LibTCPRecvedChunked((PTCP_PCB)Connection->SocketContext, Length);
        return;
    }

    LOCK_TCPIP_CORE();
    LibTCPRecvedChunked((PTCP_PCB)Connection->SocketContext, Length);
    UNLOCK_TCPIP_CORE();
}

NTSTATUS LibTCPGetDataFromConnectionQueue(PCONNECTION_ENDPOINT Connection, PUCHAR RecvBuffer, UINT RecvLen, UINT *Received, const int safe)
{
    PQUEUE_ENTRY qp;
    struct pbuf* p;
    NTSTATUS Status;
    UINT ReadLength, PayloadLength, Offset, Copied;

    (*Received) = 0;

    LockObject(Connection);

    if (!IsListEmpty(&Connection->PacketQueue))
    {
        while ((qp = LibTCPDequeuePacket(Connection)) != NULL)
        {
            p = qp->p;

            /* Calculate the payload length first */
            PayloadLength = p->tot_len;
            PayloadLength -= qp->Offset;
            Offset = qp->Offset;

            /* Check if we're reading the whole buffer */
            ReadLength = MIN(PayloadLength, RecvLen);
            ASSERT(ReadLength != 0);
            if (ReadLength != PayloadLength)
            {
                /* Save this one for later */
                qp->Offset += ReadLength;
                InsertHeadList(&Connection->PacketQueue, &qp->ListEntry);
                qp = NULL;
            }

            Copied = pbuf_copy_partial(p, RecvBuffer, ReadLength, Offset);
            ASSERT(Copied == ReadLength);

            /* Update trackers */
            RecvLen -= ReadLength;
            RecvBuffer += ReadLength;
            (*Received) += ReadLength;

            if (qp != NULL)
            {
                /* Use this special pbuf free callback function because we're outside tcpip thread */
                pbuf_free_callback(qp->p);

                ExFreeToNPagedLookasideList(&QueueEntryLookasideList, qp);
            }
            else
            {
                /* If we get here, it means we've filled the buffer */
                ASSERT(RecvLen == 0);
            }

            ASSERT((*Received) != 0);
            Status = STATUS_SUCCESS;

            if (!RecvLen)
                break;
        }
    }
    else
    {
        if (Connection->ReceiveShutdown)
            Status = Connection->ReceiveShutdownStatus;
        else
            Status = STATUS_PENDING;
    }

    UnlockObject(Connection);

    if (*Received != 0)
        LibTCPRecved(Connection, *Received, safe);

    return Status;
}

static
BOOLEAN
WaitForEventSafely(PRKEVENT Event)
{
    PVOID WaitObjects[] = {Event, &TerminationEvent};

    if (KeWaitForMultipleObjects(2,
                                 WaitObjects,
                                 WaitAny,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 NULL,
                                 NULL) == STATUS_WAIT_0)
    {
        /* Signalled by the caller's event */
        return TRUE;
    }
    else /* if KeWaitForMultipleObjects() == STATUS_WAIT_1 */
    {
        /* Signalled by our termination event */
        return FALSE;
    }
}

static
err_t
InternalSendEventHandler(void *arg, PTCP_PCB pcb, const u16_t space)
{
    /* Make sure the socket didn't get closed */
    if (!arg) return ERR_OK;

    TCPSendEventHandler(arg, space);

    return ERR_OK;
}

static
err_t
InternalRecvEventHandler(void *arg, PTCP_PCB pcb, struct pbuf *p, const err_t err)
{
    PCONNECTION_ENDPOINT Connection = arg;

    /* Make sure the socket didn't get closed */
    if (!arg)
    {
        if (p)
            pbuf_free(p);

        return ERR_OK;
    }

    if (p)
    {
        LibTCPEnqueuePacket(Connection, p);

        TCPRecvEventHandler(arg);
    }
    else if (err == ERR_OK)
    {
        /* Complete pending reads with 0 bytes to indicate a graceful closure,
         * but note that send is still possible in this state so we don't close the
         * whole socket here (by calling tcp_close()) as that would violate TCP specs
         */
        Connection->ReceiveShutdown = TRUE;
        Connection->ReceiveShutdownStatus = STATUS_SUCCESS;

        /* If we already did a send shutdown, we're in TIME_WAIT so we can't use this PCB anymore */
        if (Connection->SendShutdown)
        {
            Connection->SocketContext = NULL;
            tcp_arg(pcb, NULL);
        }

        /* Indicate the graceful close event */
        TCPRecvEventHandler(arg);

        /* If the PCB is gone, clean up the connection */
        if (Connection->SendShutdown)
        {
            TCPFinEventHandler(Connection, ERR_CLSD);
        }
    }

    return ERR_OK;
}

static
err_t
InternalPendingAcceptRecvEventHandler(void *arg,
                                      PTCP_PCB pcb,
                                      struct pbuf *p,
                                      const err_t err)
{
    UNREFERENCED_PARAMETER(arg);
    UNREFERENCED_PARAMETER(pcb);

    if (p)
        return ERR_MEM;

    return err;
}

static
void
InternalPendingAcceptErrorEventHandler(void *arg, const err_t err)
{
    PTCP_PENDING_ACCEPT Pending = arg;
    PCONNECTION_ENDPOINT Listener;

    UNREFERENCED_PARAMETER(err);

    if (!Pending)
        return;

    Listener = Pending->Listener;
    LockObject(Listener);
    RemoveEntryList(&Pending->Entry);
    UnlockObject(Listener);

    DereferenceObject(Listener);
    ExFreePoolWithTag(Pending, TCP_ACCEPT_TAG);
}

NTSTATUS
LibTCPDeferAcceptLocked(PCONNECTION_ENDPOINT Connection, PTCP_PCB pcb)
{
    PTCP_PENDING_ACCEPT Pending;

    ASSERT_TCPIP_OBJECT_LOCKED(Connection);

    Pending = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Pending),
                                    TCP_ACCEPT_TAG);
    if (!Pending)
        return STATUS_NO_MEMORY;

    Pending->Listener = Connection;
    Pending->SocketContext = pcb;
    ReferenceObject(Connection);
    InsertTailList(&Connection->PendingAccepts, &Pending->Entry);

    tcp_arg(pcb, Pending);
    tcp_recv(pcb, InternalPendingAcceptRecvEventHandler);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, InternalPendingAcceptErrorEventHandler);
    tcp_backlog_delayed(pcb);

    return STATUS_PENDING;
}

static
void
LibTCPDrainPendingAcceptCallback(void *arg)
{
    PCONNECTION_ENDPOINT Connection = arg;
    PTCP_PENDING_ACCEPT Pending;
    PTCP_PCB pcb;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    LockObject(Connection);
    if (IsListEmpty(&Connection->PendingAccepts) ||
        IsListEmpty(&Connection->ListenRequest))
    {
        UnlockObject(Connection);
        return;
    }

    Entry = RemoveHeadList(&Connection->PendingAccepts);
    Pending = CONTAINING_RECORD(Entry, TCP_PENDING_ACCEPT, Entry);
    pcb = Pending->SocketContext;
    UnlockObject(Connection);

    Status = TCPAcceptEventHandler(Connection, pcb);
    if (Status == STATUS_SUCCESS)
    {
        tcp_backlog_accepted(pcb);
    }
    else if (Status != STATUS_PENDING)
    {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_abort(pcb);
    }

    DereferenceObject(Pending->Listener);
    ExFreePoolWithTag(Pending, TCP_ACCEPT_TAG);
}

void
LibTCPDrainPendingAccept(PCONNECTION_ENDPOINT Connection)
{
    (void)tcpip_callback_wait(LibTCPDrainPendingAcceptCallback, Connection);
}

/* This function MUST return an error value that is not ERR_ABRT or ERR_OK if the connection
 * is not accepted to avoid leaking the new PCB */
static
err_t
InternalAcceptEventHandler(void *arg, PTCP_PCB newpcb, const err_t err)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(err);

    /* Make sure the socket didn't get closed */
    if (!arg)
        return ERR_CLSD;
    if (!newpcb)
        return ERR_MEM;

    Status = TCPAcceptEventHandler(arg, newpcb);

    if (NT_SUCCESS(Status))
        return ERR_OK;

    return ERR_MEM;
}

static
err_t
InternalConnectEventHandler(void *arg, PTCP_PCB pcb, const err_t err)
{
    /* Make sure the socket didn't get closed */
    if (!arg)
        return ERR_OK;

    TCPConnectEventHandler(arg, err);

    return ERR_OK;
}

static
void
InternalErrorEventHandler(void *arg, const err_t err)
{
    PCONNECTION_ENDPOINT Connection = arg;

    /* Make sure the socket didn't get closed */
    if (!arg || Connection->SocketContext == NULL) return;

    /* The PCB is dead now */
    Connection->SocketContext = NULL;

    /* Give them one shot to receive the remaining data */
    Connection->ReceiveShutdown = TRUE;
    Connection->ReceiveShutdownStatus = TCPTranslateError(err);
    TCPRecvEventHandler(Connection);

    /* Terminate the connection */
    TCPFinEventHandler(Connection, err);
}

static
void
LibTCPSocketCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;

    ASSERT(msg);

    msg->Output.Socket.NewPcb = tcp_new();

    if (msg->Output.Socket.NewPcb)
    {
        tcp_arg(msg->Output.Socket.NewPcb, msg->Input.Socket.Arg);
        tcp_err(msg->Output.Socket.NewPcb, InternalErrorEventHandler);
    }
}

struct tcp_pcb *
LibTCPSocket(void *arg)
{
    struct lwip_callback_msg msg;

    msg.Input.Socket.Arg = arg;

    if (tcpip_callback_wait(LibTCPSocketCallback, &msg) != ERR_OK)
        return NULL;

    return msg.Output.Socket.NewPcb;
}

static
void
LibTCPFreeSocketCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;

    ASSERT(msg);

    /* Calling tcp_close will free it */
    tcp_close(msg->Input.FreeSocket.pcb);

    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

void LibTCPFreeSocket(PTCP_PCB pcb)
{
    struct lwip_callback_msg msg;

    KeInitializeEvent(&msg.Event, NotificationEvent, FALSE);
    msg.Input.FreeSocket.pcb = pcb;

    tcpip_callback_with_block(LibTCPFreeSocketCallback, &msg, 1);

    WaitForEventSafely(&msg.Event);
}


static
void
LibTCPBindCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Bind.Connection->SocketContext;

    ASSERT(msg);

    if (!msg->Input.Bind.Connection->SocketContext)
    {
        msg->Output.Bind.Error = ERR_CLSD;
        goto done;
    }

    /* We're guaranteed that the local address is valid to bind at this point */
    pcb->so_options |= SOF_REUSEADDR;

    msg->Output.Bind.Error = tcp_bind(pcb,
                                      msg->Input.Bind.IpAddress,
                                      lwip_ntohs(msg->Input.Bind.Port));

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

err_t
LibTCPBind(PCONNECTION_ENDPOINT Connection, ip4_addr_t *const ipaddr, const u16_t port)
{
    struct lwip_callback_msg msg;
    err_t ret;

    KeInitializeEvent(&msg.Event, NotificationEvent, FALSE);
    msg.Input.Bind.Connection = Connection;
    msg.Input.Bind.IpAddress = ipaddr;
    msg.Input.Bind.Port = port;

    if (tcpip_callback_with_block(LibTCPBindCallback, &msg, 1) != ERR_OK)
        return ERR_MEM;

    if (WaitForEventSafely(&msg.Event))
        ret = msg.Output.Bind.Error;
    else
        ret = ERR_CLSD;

    return ret;
}

static
void
LibTCPListenCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    u8_t backlog;

    ASSERT(msg);

    if (!msg->Input.Listen.Connection->SocketContext)
    {
        msg->Output.Listen.NewPcb = NULL;
        goto done;
    }

    /* lwIP stores the backlog in a u8_t. Saturate larger TDI values. */
    backlog = msg->Input.Listen.Backlog > 0xffU ?
              0xffU : (u8_t)msg->Input.Listen.Backlog;
    msg->Output.Listen.NewPcb =
        tcp_listen_with_backlog((PTCP_PCB)msg->Input.Listen.Connection->SocketContext,
                                backlog);

    if (msg->Output.Listen.NewPcb)
    {
        tcp_accept(msg->Output.Listen.NewPcb, InternalAcceptEventHandler);
    }

done:
    KeSetEvent(&msg->Event, IO_NO_INCREMENT, FALSE);
}

PTCP_PCB
LibTCPListen(PCONNECTION_ENDPOINT Connection, const UINT backlog)
{
    struct lwip_callback_msg msg;
    PTCP_PCB ret;

    KeInitializeEvent(&msg.Event, NotificationEvent, FALSE);
    msg.Input.Listen.Connection = Connection;
    msg.Input.Listen.Backlog = backlog;

    if (tcpip_callback_with_block(LibTCPListenCallback, &msg, 1) != ERR_OK)
        return NULL;

    if (WaitForEventSafely(&msg.Event))
        ret = msg.Output.Listen.NewPcb;
    else
        ret = NULL;

    return ret;
}

static
void
LibTCPSendCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Send.Connection->SocketContext;
    ULONG SendLength;
    UCHAR SendFlags;

    ASSERT(msg);

    if (!msg->Input.Send.Connection->SocketContext)
    {
        msg->Output.Send.Error = ERR_CLSD;
        goto done;
    }

    if (msg->Input.Send.Connection->SendShutdown)
    {
        msg->Output.Send.Error = ERR_CLSD;
        goto done;
    }

    SendFlags = TCP_WRITE_FLAG_COPY;
    SendLength = msg->Input.Send.DataLength;
    if (tcp_sndbuf(pcb) == 0)
    {
        /* No buffer space so return pending */
        msg->Output.Send.Error = ERR_INPROGRESS;
        goto done;
    }
    else if (tcp_sndbuf(pcb) < SendLength)
    {
        /* We've got some room so let's send what we can */
        SendLength = tcp_sndbuf(pcb);

        /* Don't set the push flag */
        SendFlags |= TCP_WRITE_FLAG_MORE;
    }

    msg->Output.Send.Error = tcp_write(pcb,
                                       msg->Input.Send.Data,
                                       SendLength,
                                       SendFlags);
    if (msg->Output.Send.Error == ERR_OK)
    {
        /* Queued successfully so try to send it */
        tcp_output((PTCP_PCB)msg->Input.Send.Connection->SocketContext);
        msg->Output.Send.Information = SendLength;
    }
    else if (msg->Output.Send.Error == ERR_MEM)
    {
        /* The queue is too long */
        msg->Output.Send.Error = ERR_INPROGRESS;
    }

done:
    return;
}

err_t
LibTCPSend(PCONNECTION_ENDPOINT Connection, void *const dataptr, const u16_t len, ULONG *sent, const int safe)
{
    err_t ret;
    struct lwip_callback_msg msg;

    msg.Input.Send.Connection = Connection;
    msg.Input.Send.Data = dataptr;
    msg.Input.Send.DataLength = len;

    if (safe)
        LibTCPSendCallback(&msg);
    else if ((ret = tcpip_callback_wait(LibTCPSendCallback, &msg)) != ERR_OK)
    {
        *sent = 0;
        return ret;
    }

    ret = msg.Output.Send.Error;
    if (ret == ERR_OK)
        *sent = msg.Output.Send.Information;
    else
        *sent = 0;

    return ret;
}

static
void
LibTCPConnectCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    err_t Error;

    ASSERT(arg);

    if (!msg->Input.Connect.Connection->SocketContext)
    {
        msg->Output.Connect.Error = ERR_CLSD;
        goto done;
    }

    tcp_recv((PTCP_PCB)msg->Input.Connect.Connection->SocketContext, InternalRecvEventHandler);
    tcp_sent((PTCP_PCB)msg->Input.Connect.Connection->SocketContext, InternalSendEventHandler);

    Error = tcp_connect((PTCP_PCB)msg->Input.Connect.Connection->SocketContext,
                        msg->Input.Connect.IpAddress, lwip_ntohs(msg->Input.Connect.Port),
                        InternalConnectEventHandler);

    msg->Output.Connect.Error = Error == ERR_OK ? ERR_INPROGRESS : Error;

done:
    return;
}

err_t
LibTCPConnect(PCONNECTION_ENDPOINT Connection, ip_addr_t *const ipaddr, const u16_t port)
{
    struct lwip_callback_msg msg;
    err_t Error;

    msg.Input.Connect.Connection = Connection;
    msg.Input.Connect.IpAddress = ipaddr;
    msg.Input.Connect.Port = port;

    Error = tcpip_callback_wait(LibTCPConnectCallback, &msg);
    if (Error != ERR_OK)
        return Error;

    return msg.Output.Connect.Error;
}

static
void
LibTCPShutdownCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Shutdown.Connection->SocketContext;

    if (!msg->Input.Shutdown.Connection->SocketContext)
    {
        msg->Output.Shutdown.Error = ERR_CLSD;
        goto done;
    }

    /* LwIP makes the (questionable) assumption that SHUTDOWN_RDWR is equivalent to tcp_close().
     * This assumption holds even if the shutdown calls are done separately (even through multiple
     * WinSock shutdown() calls). This assumption means that lwIP has the right to deallocate our
     * PCB without telling us if we shutdown TX and RX. To avoid these problems, we'll clear the
     * socket context if we have called shutdown for TX and RX.
     */
    if (msg->Input.Shutdown.shut_rx != msg->Input.Shutdown.shut_tx) {
        if (msg->Input.Shutdown.shut_rx) {
            msg->Output.Shutdown.Error = tcp_shutdown(pcb, TRUE, FALSE);
        }
        if (msg->Input.Shutdown.shut_tx) {
            msg->Output.Shutdown.Error = tcp_shutdown(pcb, FALSE, TRUE);
        }
    }
    else if (msg->Input.Shutdown.shut_rx) {
        /* We received both RX and TX requests, which seems to mean closing connection from TDI.
         * So call tcp_close, otherwise we risk to be put in TCP_WAIT_* states, which makes further
         * attempts to close the socket to fail in this state.
         */
        msg->Output.Shutdown.Error = tcp_close(pcb);
    }
    else {
        /* This case shouldn't happen */
        DbgPrint("Requested socket shutdown(0, 0) !\n");
    }

    if (!msg->Output.Shutdown.Error)
    {
        if (msg->Input.Shutdown.shut_rx)
        {
            msg->Input.Shutdown.Connection->ReceiveShutdown = TRUE;
            msg->Input.Shutdown.Connection->ReceiveShutdownStatus = STATUS_FILE_CLOSED;
        }

        if (msg->Input.Shutdown.shut_tx)
            msg->Input.Shutdown.Connection->SendShutdown = TRUE;

        if (msg->Input.Shutdown.Connection->ReceiveShutdown &&
            msg->Input.Shutdown.Connection->SendShutdown)
        {
            /* The PCB is not ours anymore */
            msg->Input.Shutdown.Connection->SocketContext = NULL;
            tcp_arg(pcb, NULL);
            TCPFinEventHandler(msg->Input.Shutdown.Connection, ERR_CLSD);
        }
    }

done:
    return;
}

err_t
LibTCPShutdown(PCONNECTION_ENDPOINT Connection, const int shut_rx, const int shut_tx)
{
    struct lwip_callback_msg msg;
    err_t Error;

    msg.Input.Shutdown.Connection = Connection;
    msg.Input.Shutdown.shut_rx = shut_rx;
    msg.Input.Shutdown.shut_tx = shut_tx;

    Error = tcpip_callback_wait(LibTCPShutdownCallback, &msg);
    if (Error != ERR_OK)
        return Error;

    return msg.Output.Shutdown.Error;
}

static
void
LibTCPAbortPendingAccepts(PCONNECTION_ENDPOINT Connection)
{
    PTCP_PENDING_ACCEPT Pending;
    PTCP_PCB pcb;
    PLIST_ENTRY Entry;

    while (TRUE)
    {
        LockObject(Connection);
        if (IsListEmpty(&Connection->PendingAccepts))
        {
            UnlockObject(Connection);
            break;
        }

        Entry = RemoveHeadList(&Connection->PendingAccepts);
        Pending = CONTAINING_RECORD(Entry, TCP_PENDING_ACCEPT, Entry);
        pcb = Pending->SocketContext;
        UnlockObject(Connection);

        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_abort(pcb);

        DereferenceObject(Pending->Listener);
        ExFreePoolWithTag(Pending, TCP_ACCEPT_TAG);
    }
}

static
void
LibTCPCloseCallback(void *arg)
{
    struct lwip_callback_msg *msg = arg;
    PTCP_PCB pcb = msg->Input.Close.Connection->SocketContext;

    LibTCPAbortPendingAccepts(msg->Input.Close.Connection);

    /* Empty the queue even if we're already "closed" */
    LibTCPEmptyQueue(msg->Input.Close.Connection);

    /* Check if we've already been closed */
    if (msg->Input.Close.Connection->Closing)
    {
        msg->Output.Close.Error = ERR_OK;
        goto done;
    }

    /* Enter "closing" mode if we're doing a normal close */
    if (msg->Input.Close.Callback)
        msg->Input.Close.Connection->Closing = TRUE;

    /* Check if the PCB was already "closed" but the client doesn't know it yet */
    if (!msg->Input.Close.Connection->SocketContext)
    {
        msg->Output.Close.Error = ERR_OK;
        goto done;
    }

    /* Clear the PCB pointer and stop callbacks */
    msg->Input.Close.Connection->SocketContext = NULL;
    tcp_arg(pcb, NULL);

    /* This may generate additional callbacks but we don't care,
     * because they're too inconsistent to rely on */
    msg->Output.Close.Error = tcp_close(pcb);

    if (msg->Output.Close.Error)
    {
        /* Restore the PCB pointer */
        msg->Input.Close.Connection->SocketContext = pcb;
        msg->Input.Close.Connection->Closing = FALSE;
    }
    else if (msg->Input.Close.Callback)
    {
        TCPFinEventHandler(msg->Input.Close.Connection, ERR_CLSD);
    }

done:
    return;
}

err_t
LibTCPClose(PCONNECTION_ENDPOINT Connection, const int safe, const int callback)
{
    struct lwip_callback_msg msg;
    err_t Error;

    msg.Input.Close.Connection = Connection;
    msg.Input.Close.Callback = callback;

    if (safe)
        LibTCPCloseCallback(&msg);
    else
    {
        Error = tcpip_callback_wait(LibTCPCloseCallback, &msg);
        if (Error != ERR_OK)
            return Error;
    }

    return msg.Output.Close.Error;
}

void
LibTCPAccept(PTCP_PCB pcb, struct tcp_pcb *listen_pcb, void *arg)
{
    ASSERT(arg);

    tcp_arg(pcb, NULL);
    tcp_recv(pcb, InternalRecvEventHandler);
    tcp_sent(pcb, InternalSendEventHandler);
    tcp_err(pcb, InternalErrorEventHandler);
    tcp_arg(pcb, arg);

    tcp_accepted(listen_pcb);
}

err_t
LibTCPGetHostName(PTCP_PCB pcb, ip_addr_t *const ipaddr, u16_t *const port)
{
    if (!pcb)
        return ERR_CLSD;

    *ipaddr = pcb->local_ip;
    *port = pcb->local_port;

    return ERR_OK;
}

err_t
LibTCPGetPeerName(PTCP_PCB pcb, ip_addr_t * const ipaddr, u16_t * const port)
{
    if (!pcb)
        return ERR_CLSD;

    *ipaddr = pcb->remote_ip;
    *port = pcb->remote_port;

    return ERR_OK;
}

void
LibTCPSetNoDelay(
    PTCP_PCB pcb,
    BOOLEAN Set)
{
    if (Set)
        pcb->flags |= TF_NODELAY;
    else
        pcb->flags &= ~TF_NODELAY;
}

void
LibTCPSetKeepAlive(
    PTCP_PCB pcb,
    BOOLEAN Set)
{
    if (Set)
        pcb->so_options |= SOF_KEEPALIVE;
    else
        pcb->so_options &= ~SOF_KEEPALIVE;
}

void
LibTcpSetKeepAliveValues(
    PTCP_PCB pcb,
    u32_t KeepAliveTime,
    u32_t KeepAliveInterval
)
{
    pcb->keep_idle = KeepAliveTime;
    pcb->keep_intvl = KeepAliveInterval;
    pcb->keep_cnt = 10;
}

void
LibTCPGetSocketStatus(
    PTCP_PCB pcb,
    PULONG State)
{
    /* Translate state from enum tcp_state -> MIB_TCP_STATE */
    *State = pcb->state + 1;
}
