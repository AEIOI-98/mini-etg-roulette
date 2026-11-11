import { defineConfig } from 'vite'

// Relative assets work both at localhost and under a GitHub Pages project path.
export default defineConfig({ base: './', build: { target: 'es2022' } })
