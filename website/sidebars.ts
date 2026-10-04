import type { SidebarsConfig } from '@docusaurus/plugin-content-docs';

// Explicit order. The pages carry sidebar_position too, but this array is what
// actually decides, and reading the intended reading-order in one place beats
// inferring it from seven frontmatter blocks.
const sidebars: SidebarsConfig = {
  docsSidebar: [
    'intro',
    {
      type: 'category',
      label: 'Getting started',
      collapsed: false,
      items: ['installing', 'tutorial'],
    },
    {
      type: 'category',
      label: 'Understanding Lockstep',
      collapsed: false,
      items: ['paradigm', 'glossary'],
    },
    {
      type: 'category',
      label: 'Reference',
      collapsed: false,
      items: ['reference', 'gestures', 'status'],
    },
    'troubleshooting',
    'not-done',
  ],
};

export default sidebars;
