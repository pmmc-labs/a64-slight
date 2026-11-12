;; ping-pong: n round trips between two go blocks, each reading a channel
;; of its own as its mailbox (core.async).
(ns ping-pong
    (:require [clojure.core.async :refer [chan go-loop <! >! <!!]])
    (:gen-class))

(defn pong [in]
    (go-loop []
        (let [[tag from i] (<! in)]
            (when (= tag :ping)
                (>! from [:pong i])
                (recur)))))

(defn ping [p n]
    (let [me (chan 1)]
        (go-loop [i 0]
            (if (= i n)
                (do (>! p [:stop]) n)
                (do (>! p [:ping me i]) (<! me) (recur (inc i)))))))

(defn -main [arg]
    (let [p (chan 1)]
        (pong p)
        (println (<!! (ping p (Long/parseLong arg))))
        (System/exit 0)))
