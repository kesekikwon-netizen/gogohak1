# GitHub 토큰 보관

## 기본: 토큰 파일 없이 쓰기 (권장)

이 PC 는 Git 자격 증명 관리자(`credential.helper=manager`)와 `gh` 키링을 쓴다.
토큰이 OS 보호 저장소에 들어가므로 평문 파일이 필요 없다. 평소 `git push` 는
이 방식 그대로 쓰면 된다.

```powershell
gh auth login          # 필요할 때만
gh auth setup-git      # git 이 gh 자격 증명을 쓰게 한다
```

## 토큰 파일이 필요할 때

스크립트나 도구가 `GH_TOKEN` 환경변수를 요구할 때만 쓴다.

1. 템플릿을 저장소 밖으로 복사한다.

   ```powershell
   Copy-Item .\.env.example "$env:USERPROFILE\.config\ka-hgis\git.env"
   ```

2. `GH_TOKEN=` 뒤에 토큰을 붙여 넣는다. 따옴표는 쓰지 않는다.

3. 본인만 읽도록 권한을 잠근다.

   ```powershell
   icacls "$env:USERPROFILE\.config\ka-hgis\git.env" /inheritance:r /grant:r "$($env:USERNAME):F"
   ```

4. 필요한 세션에서만 불러온다. 점 소스로 실행해야 환경변수가 남는다.

   ```powershell
   . .\scripts\load-git-env.ps1
   . .\scripts\load-git-env.ps1 -Check
   ```

로더는 값을 출력하지 않고, 파일이 저장소 안에 있거나 다른 계정이 접근할 수 있으면
거부하거나 경고한다.

## 토큰 발급 기준

- fine-grained personal access token 을 쓴다.
- 대상 저장소는 `kesekikwon-netizen/gogohak1` 하나만 고른다.
- 권한은 Contents = Read and write 만 준다. 필요하면 Pull requests 를 더한다.
- 만료는 90일로 둔다.

## 하지 말 것

- 원격 주소에 토큰을 넣지 않는다. `https://<토큰>@github.com/...` 는 `.git/config`
  와 셸 기록에 평문으로 남는다.
- 토큰을 커밋하지 않는다. `.env` 와 `*.env` 는 무시 목록에 있고 `.env.example` 만
  추적한다.
- 토큰을 화면 공유나 로그에 노출하지 않는다.

## 유출이 의심되면

1. GitHub → Settings → Developer settings 에서 해당 토큰을 즉시 폐기한다.
2. 새 토큰을 발급해 환경 파일만 교체한다.
3. 저장소 이력에 값이 들어갔다면 폐기가 우선이고, 이력 정리는 그다음이다.
