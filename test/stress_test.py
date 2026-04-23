import pika
import json
import threading
import time
import random

def send_burst(thread_id, count):
    connection = pika.BlockingConnection(pika.ConnectionParameters('127.0.0.1'))
    channel = connection.channel()
    
    intents = ["dj.skip", "dj.status_request", "dj.set_playlist"]
    
    for i in range(count):
        intent = random.choice(intents)
        payload = {"uri": "http://stream.test/audio.mp3", "ts": time.time()}
        
        channel.basic_publish(
            exchange='radio.events',
            routing_key=intent,
            body=json.dumps(payload)
        )
        if i % 10 == 0:
            print(f"Thread {thread_id} sent {i} messages")
            
    connection.close()

if __name__ == "__main__":
    threads = []
    # 10 threads enviando 100 mensagens cada (1000 total)
    for i in range(10):
        t = threading.Thread(target=send_burst, args=(i, 100))
        threads.append(t)
        t.start()
        
    for t in threads:
        t.join()
    print("Stress test completed.")
