<script setup lang="ts">
import { ref, watch } from 'vue'

const props = defineProps<{
  visible: boolean
  theme: 'dark' | 'light'
}>()

const emit = defineEmits<{
  close: []
  updateTheme: [theme: 'dark' | 'light']
}>()

const currentTheme = ref(props.theme)

watch(() => props.theme, (v) => { currentTheme.value = v })

function setTheme(t: 'dark' | 'light') {
  currentTheme.value = t
  emit('updateTheme', t)
}
</script>

<template>
  <Teleport to="body">
    <Transition name="overlay">
      <div v-if="visible" class="settings-overlay" @click.self="emit('close')">
        <Transition name="modal">
          <div v-if="visible" class="settings-modal">
            <div class="settings-header">
              <span class="settings-icon">⚙️</span>
              <span class="settings-title">设置</span>
              <button class="btn-close" @click="emit('close')">×</button>
            </div>

            <div class="settings-body">
              <!-- 主题设置 -->
              <div class="settings-group">
                <div class="group-label">外观</div>
                <div class="theme-options">
                  <button
                    class="theme-btn"
                    :class="{ active: currentTheme === 'dark' }"
                    @click="setTheme('dark')"
                  >
                    <span class="theme-preview dark-preview">
                      <span class="preview-bar"></span>
                      <span class="preview-content"></span>
                    </span>
                    <span class="theme-name">暗色</span>
                  </button>
                  <button
                    class="theme-btn"
                    :class="{ active: currentTheme === 'light' }"
                    @click="setTheme('light')"
                  >
                    <span class="theme-preview light-preview">
                      <span class="preview-bar"></span>
                      <span class="preview-content"></span>
                    </span>
                    <span class="theme-name">亮色</span>
                  </button>
                </div>
              </div>

              <!-- 关于 -->
              <div class="settings-group">
                <div class="group-label">关于</div>
                <div class="about-info">
                  <div class="about-item">
                    <span class="about-label">版本</span>
                    <span class="about-value">1.0.0</span>
                  </div>
                  <div class="about-item">
                    <span class="about-label">技术栈</span>
                    <span class="about-value">C++17 + Vue 3</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </Transition>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.settings-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.settings-modal {
  width: 400px;
  max-width: 90vw;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  box-shadow: var(--shadow-xl);
  overflow: hidden;
}

.settings-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 16px 20px;
  border-bottom: 1px solid var(--border);
}

.settings-icon {
  font-size: 18px;
}

.settings-title {
  flex: 1;
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
}

.btn-close {
  width: 28px;
  height: 28px;
  border: none;
  border-radius: var(--radius-sm);
  background: transparent;
  color: var(--text-muted);
  font-size: 18px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all var(--duration-fast);
}

.btn-close:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.settings-body {
  padding: 16px 20px;
}

.settings-group {
  margin-bottom: 20px;
}

.settings-group:last-child {
  margin-bottom: 0;
}

.group-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 12px;
}

.theme-options {
  display: flex;
  gap: 12px;
}

.theme-btn {
  flex: 1;
  padding: 12px;
  border: 2px solid var(--border);
  border-radius: var(--radius-md);
  background: transparent;
  cursor: pointer;
  transition: all var(--duration-fast) var(--ease-out);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
}

.theme-btn:hover {
  border-color: var(--text-muted);
}

.theme-btn.active {
  border-color: var(--accent);
  background: var(--accent-subtle);
}

.theme-preview {
  width: 100%;
  height: 60px;
  border-radius: var(--radius-sm);
  overflow: hidden;
  display: flex;
  flex-direction: column;
}

.dark-preview {
  background: #1a1b26;
}

.dark-preview .preview-bar {
  height: 16px;
  background: #16161e;
}

.dark-preview .preview-content {
  flex: 1;
  margin: 6px;
  border-radius: 4px;
  background: #292e42;
}

.light-preview {
  background: #ffffff;
}

.light-preview .preview-bar {
  height: 16px;
  background: #f7f8fa;
  border-bottom: 1px solid #dde1e8;
}

.light-preview .preview-content {
  flex: 1;
  margin: 6px;
  border-radius: 4px;
  background: #eef0f4;
}

.theme-name {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-secondary);
}

.theme-btn.active .theme-name {
  color: var(--accent);
}

.about-info {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.about-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 12px;
  background: var(--bg-primary);
  border-radius: var(--radius-sm);
}

.about-label {
  font-size: 13px;
  color: var(--text-muted);
}

.about-value {
  font-size: 13px;
  color: var(--text-secondary);
  font-weight: 500;
}
</style>
