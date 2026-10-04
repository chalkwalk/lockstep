import type { ReactNode } from 'react';
import clsx from 'clsx';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

type FeatureItem = {
  title: string;
  description: ReactNode;
};

const FeatureList: FeatureItem[] = [
  {
    title: 'One grammar, not a hundred shortcuts',
    description: (
      <>
        Every gesture is a <strong>scope</strong> plus a <strong>verb</strong>:
        hold what you mean, press what to do. Eight modifiers and five verbs
        compose, so learning one more scope teaches you every verb on it at
        once. Adding a bespoke single-purpose button is forbidden &mdash; if a
        feature would need its own key, it is not ready.
      </>
    ),
  },
  {
    title: 'Learned by hand, read by colour',
    description: (
      <>
        Each scope owns a colour, and the surface is lit by it. A fluent player
        works without looking at the screen, which is the yardstick the design
        is held to rather than a happy accident of it.
      </>
    ),
  },
  {
    title: 'Every track picks its own engine',
    description: (
      <>
        A track can host a sampler, an FM or analogue voice, a drum synth, a
        General&nbsp;MIDI player, a looper, a tape deck or a MIDI-out adapter.
        The sequencer treats them identically, and internal audio and external
        gear are equal citizens.
      </>
    ),
  },
  {
    title: 'The gestures that edit are the gestures that perform',
    description: (
      <>
        There is no design mode and no performance mode. Per-step parameter
        locks, trig conditions, fills and mutes are all played live, on the
        same keys, while the sequence is running.
      </>
    ),
  },
  {
    title: 'Plugin and standalone, equally',
    description: (
      <>
        VST3, CLAP and a standalone application, plus AU on macOS. Neither is
        the afterthought: the standalone has its own project files, file bar
        and quit guard, and a feature that needs a special case for either is
        probably wrong.
      </>
    ),
  },
  {
    title: 'Honest about what is proven',
    description: (
      <>
        Linux is developed and tested. macOS compiles and passes the whole
        suite in CI but has never been loaded in a host. Windows compiles, with
        two GUI suites unrun rather than passing. That table is on the{' '}
        <a href="/docs/">front page of the docs</a>, not buried.
      </>
    ),
  },
];

function Feature({ title, description }: FeatureItem) {
  return (
    <div className={clsx('col col--6')}>
      <div className={styles.feature}>
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures(): ReactNode {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
