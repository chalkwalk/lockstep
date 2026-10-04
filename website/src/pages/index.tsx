import type { ReactNode } from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import HomepageFeatures from '@site/src/components/HomepageFeatures';
import Heading from '@theme/Heading';

import styles from './index.module.css';

function HomepageHeader() {
  const { siteConfig } = useDocusaurusContext();
  return (
    <header className={clsx('hero', styles.heroBanner)}>
      <div className="container">
        {/* Decorative: the title below says the name already. */}
        <img src="img/logo-mark.svg" alt="" className={styles.heroLogo} />
        <Heading as="h1" className="hero__title">
          {siteConfig.title}
        </Heading>
        <p className="hero__subtitle">{siteConfig.tagline}</p>
        <div className={styles.buttons}>
          <Link className="button button--primary button--lg" to="/docs/tutorial">
            Your first pattern
          </Link>
          <Link className="button button--secondary button--lg" to="/docs/paradigm">
            How it thinks
          </Link>
        </div>
        <div className={clsx('margin-top--lg', styles.heroScreenshot)}>
          {/* The real surface, not a mockup: this is a blessed scene golden
              from the test suite, so it cannot drift from what the app
              actually draws without a test failing first. */}
          <img
            src="img/lockstep-surface.png"
            alt="The Lockstep window: a transport and timeline above, a machine
                 parameter panel, a row of sixteen tracks, and the four-row
                 keyboard surface colour-coded by scope — Func and Track amber
                 and cyan, Phrase and Scene violet and green, Morph and Song,
                 Mute and Fill red and green."
            className={styles.screenshotImage}
          />
        </div>
      </div>
    </header>
  );
}

export default function Home(): ReactNode {
  return (
    <Layout
      title="A step sequencer you play, not configure"
      description="Lockstep is a performance-oriented step sequencer: one scope-and-verb grammar across the whole surface, per-step parameter locks, and a track that can host any sound engine. VST3, CLAP and standalone, with AU on macOS.">
      <HomepageHeader />
      <main>
        <HomepageFeatures />
      </main>
    </Layout>
  );
}
