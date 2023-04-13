"""1. 超时：如果消息在一定时间内不能被消费者消费，就有可能超时，变成死信。
2. 消费者拒绝消息：当消息到达消费者时，如果消费者无法处理该消息，可以拒绝接收此消息。这个时候，RabbitMQ 将会把该消息推送到 DLQ 中。
3. 队列达到最大长度：当队列中的消息数量达到最大限制时，新到达的消息将无法加入队列中，这些消息也会被转移到 DLQ 中。
4. 消息过期：RabbitMQ 可以设置消息的过期时间，如果消息超过了设定的有效期，那么 RabbitMQ 会自动将其从队列中删除，并将其推送到 DLQ 中。
5. 队列被删除：当队列被删除前，里面的所有消息都会被转移到 DLQ 中。"""
