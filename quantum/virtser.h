#pragma once

void virtser_init(void);

/* Define this function in your code to process incoming bytes */
void virtser_recv(const uint8_t ch);

/* Call this to send a character over the Virtual Serial Device */
void virtser_send(const uint8_t byte);

/* send non blocking */
void virtser_send_nonblock(const uint8_t byte);
