from flask import Flask, request
import os
import datetime

app = Flask(__name__)
LOG_DIR = 'logs'
MAX_LINES = 1000

# Buat folder logs jika belum ada
os.makedirs(LOG_DIR, exist_ok=True)

def trim_log_file(filepath, max_lines=100):
    """Keep only the last `max_lines` lines in the file."""
    if not os.path.exists(filepath):
        return

    with open(filepath, 'r') as f:
        lines = f.readlines()

    if len(lines) > max_lines:
        # Keep only the last max_lines
        with open(filepath, 'w') as f:
            f.writelines(lines[-max_lines:])

@app.route('/webhook', methods=['POST'])
def webhook():
    client_id = request.form.get('client_id', 'unknown').strip()
    data = request.form.get('data', '')

    # Validasi panjang data harus 69 karakter
    if len(data) != 69:
        return 'Invalid data length', 400

    # Hapus semua karakter newline dan carriage return
    data = data.replace('\r', '{r}').replace('\n', '{n}')

    # Opsional: batasi data hanya karakter printabel (opsional, bisa dihapus)
    # data = ''.join(c for c in data if c.isprintable() or c in ' \t')

    timestamp = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    filename = os.path.join(LOG_DIR, f'{client_id}.txt')

    # Tulis baris baru
    with open(filename, 'a') as f:
        f.write(f"[{timestamp}] {data}\n")

    # Pangkas file ke 100 baris terakhir
    trim_log_file(filename, MAX_LINES)

    # Opsional: log ke konsol (bisa dihapus untuk stealth)
    print(f"Received from {client_id}: {data[:50]}...")

    return 'OK', 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=6000, debug=False)