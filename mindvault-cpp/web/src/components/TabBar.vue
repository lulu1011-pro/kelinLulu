<script setup lang="ts">
import type { Note } from '../api'

const props = defineProps<{
  tabs: Note[]
  activeId: number | null
}>()

const emit = defineEmits<{
  select: [note: Note]
  close: [id: number]
}>()
</script>

<template>
  <div class="tab-bar" v-if="tabs.length > 0">
    <div
      v-for="note in tabs"
      :key="note.id"
      class="tab-item"
      :class="{ active: note.id === activeId }"
      @click="emit('select', note)"
    >
      <span class="tab-icon">📄</span>
      <span class="tab-title">{{ note.title || '无标题' }}</span>
      <button
        class="tab-close"
        @click.stop="emit('close', note.id)"
        title="关闭"
      >×</button>
    </div>
  </div>
</template>

<style scoped>
.tab-bar {
  display: flex;
  align-items: stretch;
  height: 36px;
  background: var(--bg-secondary);
  border-bottom: 1px solid var(--border);
  overflow-x: auto;
  overflow-y: hidden;
  flex-shrink: 0;
}

.tab-bar::-webkit-scrollbar {
  height: 2px;
}

.tab-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 0 12px;
  min-width: 100px;
  max-width: 180px;
  border-right: 1px solid var(--border);
  cursor: pointer;
  transition: all var(--duration-fast) var(--ease-out);
  position: relative;
  flex-shrink: 0;
}

.tab-item:hover {
  background: var(--bg-hover);
}

.tab-item.active {
  background: var(--bg-primary);
}

.tab-item.active::after {
  content: '';
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: var(--accent);
}

.tab-icon {
  font-size: 12px;
  flex-shrink: 0;
}

.tab-title {
  flex: 1;
  font-size: 12px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tab-item.active .tab-title {
  color: var(--text-primary);
  font-weight: 500;
}

.tab-close {
  width: 18px;
  height: 18px;
  border: none;
  border-radius: 4px;
  background: transparent;
  color: var(--text-muted);
  font-size: 14px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  opacity: 0;
  transition: all var(--duration-fast);
  flex-shrink: 0;
  padding: 0;
  line-height: 1;
}

.tab-item:hover .tab-close {
  opacity: 1;
}

.tab-close:hover {
  background: var(--bg-active);
  color: var(--text-primary);
}
</style>
