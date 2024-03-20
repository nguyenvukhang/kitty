// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"container/list"
	"sync"
)

type LRUCache[K comparable, V any] struct {
	data     map[K]V
	lock     sync.RWMutex
	max_size int
	lru      *list.List
}

func NewLRUCache[K comparable, V any](max_size int) *LRUCache[K, V] {
	ans := LRUCache[K, V]{data: map[K]V{}, max_size: max_size, lru: list.New()}
	return &ans
}

func (self *LRUCache[K, V]) MustGetOrCreate(key K, create func(key K) V) V {
	self.lock.RLock()
	ans, found := self.data[key]
	self.lock.RUnlock()
	if found {
		return ans
	}
	ans = create(key)
	self.lock.Lock()
	self.data[key] = ans
	self.lock.Unlock()
	return ans
}
