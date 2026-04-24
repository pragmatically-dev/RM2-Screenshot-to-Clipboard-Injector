import paramiko
import os

HOST = '10.11.99.1'
USER = 'root'
PASS = 'ngJ7BlCwD3' #It's bad, I know, but this device is not exposed to the WAN.
LOCAL_SO = os.path.join(os.path.dirname(__file__), 'clipboard-injector.so')
REMOTE_SO = '/home/root/xovi/extensions.d/clipboard-injector.so'

def deploy():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        print(f"Connecting to {HOST}...")
        client.connect(HOST, username=USER, password=PASS, timeout=10)
        
        # Upload .so via SFTP
        sftp = client.open_sftp()
        local_size = os.path.getsize(LOCAL_SO)
        print(f"Uploading {LOCAL_SO} ({local_size} bytes) -> {REMOTE_SO}")
        sftp.put(LOCAL_SO, REMOTE_SO)
        remote_stat = sftp.stat(REMOTE_SO)
        print(f"Upload complete: {remote_stat.st_size} bytes on device")
        sftp.close()
        
        # Restart xochitl
        print("Restarting xochitl...")
        stdin, stdout, stderr = client.exec_command("systemctl restart xochitl")
        exit_code = stdout.channel.recv_exit_status()
        err = stderr.read().decode('utf-8', errors='replace')
        if exit_code == 0:
            print("OK xochitl restarted successfully")
        else:
            print(f"FAIL restart failed (exit {exit_code}): {err}")
            
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.close()

if __name__ == "__main__":
    deploy()
