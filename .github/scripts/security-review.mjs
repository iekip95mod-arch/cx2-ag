export function isSecurityReview(review) {
  return review?.state?.toUpperCase() === 'COMMENTED' && review.user?.type === 'Bot' && review.user.login === 'github-advanced-security[bot]' && review.user.id === 62310815;
}

export async function hasSecurityFindings(repository, pr, review, api) {
  if (!isSecurityReview(review)) return false;
  let found = false;
  for (let page = 1; page <= 10; page++) {
    const comments = await api('GET', `repos/${repository}/pulls/${pr}/reviews/${review.id}/comments?per_page=100&page=${page}`);
    if (!Array.isArray(comments)) throw Error('Invalid security review comments');
    for (const comment of comments) {
      if (comment.commit_id !== review.commit_id || comment.pull_request_review_id !== review.id || comment.user?.login !== review.user.login || comment.user.id !== review.user.id || comment.user.type !== 'Bot') continue;
      if (new RegExp(`https://github\\.com/${repository}/security/code-scanning/[1-9][0-9]*(?=[)\\s]|$)`).test(comment.body ?? '')) found = true;
    }
    if (comments.length < 100) return found;
  }
  throw Error('Security review comments exceed the lookup limit');
}
