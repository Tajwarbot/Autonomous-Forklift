import cv2
from pyzbar.pyzbar import decode
import numpy as np

cap = cv2.VideoCapture(0)

print("Starting QR Code Scanner with ZBar... Press 'q' to quit.")

while True:
    ret, img = cap.read()
    
    if not ret or img is None:
        if cv2.waitKey(100) == ord("q"):
            break
        continue
    
    detected_codes = decode(img)
    
    for code in detected_codes:
        
        data = code.data.decode('utf-8')
        print("Data found: ", data)
        
        pts = np.array([point for point in code.polygon], dtype=np.int32)
        pts = pts.reshape((-1, 1, 2))
        
        cv2.polylines(img, [pts], isClosed=True, color=(255, 0, 0), thickness=2)
        
        
        x, y, w, h = code.rect
        text_y = y - 10 if (y - 10) > 20 else y + 30
        
        
        cv2.putText(img, data, (x, text_y), cv2.FONT_HERSHEY_SIMPLEX, 
                    0.8, (255, 250, 120), 2)
        

    cv2.imshow("ZBar Code Detector", img)
    

    if cv2.waitKey(1) == ord("q"):
        break


cap.release()
cv2.destroyAllWindows()