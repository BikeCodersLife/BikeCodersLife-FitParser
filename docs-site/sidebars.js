/**
 * Creating a sidebar enables you to:
 - create an ordered group of docs
 - render a sidebar for each doc of that group
 - provide next/previous navigation

 The sidebars can be generated from the filesystem, or explicitly defined here.

 Create as many sidebars as you want.
 */

// @ts-check

/** @type {import('@docusaurus/plugin-content-docs').SidebarsConfig} */
const sidebars = {
  docsSidebar: [
    {
      type: 'doc',
      id: 'intro',
      label: '🏠 Introduction',
    },
    {
      type: 'category',
      label: '🚀 Getting Started',
      items: [
        'getting-started/installation',
        'getting-started/usage',
      ],
    },
    {
      type: 'category',
      label: '⚡ Performance',
      items: [
        'performance/benchmarks',
        'performance/optimization',
      ],
    },
    {
      type: 'category',
      label: '🔒 Security',
      items: [
        'security/security-layers',
      ],
    },
    {
      type: 'category',
      label: '📚 Reference',
      items: [
        'reference/cli-options',
        'reference/json-output',
      ],
    },
  ],
};

module.exports = sidebars;
