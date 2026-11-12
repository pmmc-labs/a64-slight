;; idle-memory: n go blocks, each parked on a channel of its own, then each
;; asked for 1 and ending; the answers summed. The answers' channel holds
;; them all, as a mailbox would (as idle-memory.go's does).
(ns idle-memory
    (:require [clojure.core.async :refer [chan go <! >! >!! <!!]])
    (:gen-class))

(defn idle []
    (let [c (chan)]
        (go (let [[_ from] (<! c)] (>! from 1)))
        c))

(defn -main [arg]
    (let [n (Long/parseLong arg)
          reply (chan n)]
        (doseq [c (doall (repeatedly n idle))] (>!! c [:ping reply]))
        (println (loop [i 0 sum 0] (if (= i n) sum (recur (inc i) (+ sum (<!! reply))))))
        (System/exit 0)))
